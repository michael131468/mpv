/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdarg.h>
#include <pthread.h>
#include <assert.h>

#include "common/common.h"
#include "common/global.h"
#include "common/msg.h"

#include "osdep/pipe.h"
#include "player/client.h"
#include "player/core.h"

#include "input.h"
#include "slave_mode.h"
#include "libmpv/client.h"

#define LINE_BUF 8192
#define WRITE_BUF 512

struct slave_client {
    struct mp_log *log;
    struct mpv_global *global;
    mpv_handle *client;
    struct mp_pipe *pipe;
    bool quit;
    int state; // STATE_*, command parser state machine
    int line_pos;
    char line_buf[LINE_BUF + 1]; // +1 for additional \0 termination
    int write_pos;
    char write_buf[WRITE_BUF];
    const char *cmd_args[MP_CMD_MAX_ARGS + 1]; // +1 for implicit NULL termination
    int num_cmd_args;
    int64_t user_id;
};

enum {
    STATE_NORM = 0,
    STATE_CMD = 1,
};

static void write_data_unbuffered(struct slave_client *ctx, void *buf, size_t s)
{
    while (!ctx->quit && s > 0) {
        int r = mp_pipe_write(ctx->pipe, buf, s);
        MP_WARN(ctx, "write ret: %d->%d \n", s, r);
        if (r < 0)
            MP_FATAL(ctx, "Write error.\n");
        if (r <= 0) {
            // error or pipe closed - just ignore this
            return;
        }
        buf = (char *)buf + r;
        s -= r;
    }
}

static void write_flush(struct slave_client *ctx)
{
    write_data_unbuffered(ctx, ctx->write_buf, ctx->write_pos);
    ctx->write_pos = 0;
}

static void write_data(struct slave_client *ctx, void *buf, size_t s)
{
    if (s >= WRITE_BUF && ctx->write_pos == 0) {
        write_data_unbuffered(ctx, buf, s);
    } else {
        // All this code is for buffering to reduce write calls.
        size_t left = MPMIN(WRITE_BUF - ctx->write_pos, s);
        memcpy(ctx->write_buf + ctx->write_pos, buf, left);
        ctx->write_pos += left;
        buf = (char *)buf + left;
        s -= left;
        if (ctx->write_pos == WRITE_BUF) {
            write_flush(ctx);

            if (s > 0) {
                assert(ctx->write_pos == 0);
                if (s < WRITE_BUF) {
                    memcpy(ctx->write_buf, buf, s);
                    ctx->write_pos = s;
                } else {
                    write_data_unbuffered(ctx, buf, s);
                }
            }
        }
    }
}

static void write_vf(struct slave_client *ctx, const char *fmt, va_list ap)
{
    size_t left;
    int len;

    va_list copy;
    va_copy(copy, ap);
    left = WRITE_BUF - ctx->write_pos;
    len = vsnprintf(ctx->write_buf + ctx->write_pos, left, fmt, copy);
    va_end(copy);

    if (len + 1 < left) {
        // this was ok
        ctx->write_pos += len;
    } else {
        // buffer too small
        char *tmp = talloc_vasprintf(NULL, fmt, ap);
        write_data(ctx, tmp, strlen(tmp));
        talloc_free(tmp);
    }
}

static void write_f(struct slave_client *ctx, const char *fmt, ...)
    PRINTF_ATTRIBUTE(2, 3);

static void write_f(struct slave_client *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    write_vf(ctx, fmt, ap);
    va_end(ap);
}

static int from_hex(unsigned char c)
{
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= '0' && c <= '9')
        return c - '0';
    return -1;
}

// Write '%' escaped.
static void write_escaped(struct slave_client *ctx, const char *s)
{
    bstr b = bstr0(s);
    while (b.len) {
        bstr rest = {0};
        unsigned char c = b.start[0];
        if (c < 32 || c == '%' || bstr_decode_utf8(b, &rest) < 0) {
            static const char hex[] = "0123456789ABCDEF";
            char buf[3];
            buf[0] = '%';
            buf[1] = hex[c / 16];
            buf[2] = hex[c % 16];
            write_data(ctx, buf, 3);
            b = bstr_cut(b, 1);
        } else {
            write_data(ctx, b.start, b.len - rest.len);
            b = bstr_cut(b, b.len - rest.len);
        }
    }
}

// Undo '%' escaping
static void unescape_inplace(char *buf)
{
    int len = strlen(buf);
    int o = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = buf[i];
        if (c == '%' && i + 2 < len) {
            int c1 = from_hex(buf[i + 1]);
            int c2 = from_hex(buf[i + 2]);
            if (c1 >= 0 && c2 >= 0) {
                c = c1 * 16 + c2;
                i = i + 2;
            }
        }
        buf[o++] = c;
    }
    buf[o++] = '\0';
}

static bool match_cmd(char *line, char *cmd)
{
    int cmdlen = strlen(cmd);
    return strncmp(line, cmd, cmdlen) == 0 &&
           (line[cmdlen] == '=' || !line[cmdlen]);
}

// Note: mutates the line buffer (within its bounds)
static void process_line(struct slave_client *ctx, char *line)
{
    MP_WARN(ctx, "got line: >%s<\n", line);
    if (ctx->state == STATE_NORM) {
        if (match_cmd(line, "new_protocol")) {
            // this is the entry point for the STATE_CMD protocol
            ctx->state = STATE_CMD;
        } else {
            mpv_command_string(ctx->client, line);
        }
    }
    if (ctx->state == STATE_CMD) {
        char *split = strchr(line, '=');
        char *arg = "";
        if (split) {
            arg = split + 1;
            unescape_inplace(arg);
        }
        if (match_cmd(line, "cmd_start")) {
            for (int n = 0; n < ctx->num_cmd_args; n++) {
                talloc_free((char *)ctx->cmd_args[n]);
                ctx->cmd_args[n] = NULL;
            }
            ctx->num_cmd_args = 0;
            ctx->user_id = -1;
        } else if (match_cmd(line, "arg")) {
            if (ctx->num_cmd_args == MP_CMD_MAX_ARGS) {
                MP_ERR(ctx, "Too many arguments to command.\n");
            } else {
                ctx->cmd_args[ctx->num_cmd_args++] = talloc_strdup(ctx, arg);
            }
        } else if (match_cmd(line, "cmd_end")) {
            int r = mpv_command(ctx->client, ctx->cmd_args);
        } else if (match_cmd(line, "ping")) {
            write_f(ctx, "pong=");
            write_escaped(ctx, arg);
            write_f(ctx, "\n");
        } else {
            //if (bstr_strip(bstr0(line)).len == 0)
                MP_ERR(ctx, "unknown command: '%s'\n", line);
        }
    }
    write_flush(ctx);
}

static void check_mpv_events(struct slave_client *ctx)
{
    mpv_event *ev = mpv_wait_event(ctx->client, 0);
    switch (ev->event_id) {
    case MPV_EVENT_SHUTDOWN:
        // This forcibly disconnects the pipe.
        talloc_free(ctx->pipe);
        ctx->pipe = NULL;
        ctx->quit = true;
        break;
    default: ;
    }
}

static int slave_loop(struct slave_client *ctx)
{
    while (!ctx->quit) {
        int r = mp_pipe_wait(ctx->pipe, MP_PIPE_READ);
        if (r < 0) {
            MP_FATAL(ctx, "Poll error.\n");
            break;
        }
        if (r & MP_PIPE_READ) {
            int rest = LINE_BUF - ctx->line_pos;
            if (rest == 0) {
                MP_FATAL(ctx, "Line too long (over %d bytes).\n", LINE_BUF);
                break;
            }
            r = mp_pipe_read(ctx->pipe, ctx->line_buf + ctx->line_pos, rest);
            if (r <= 0) {
                if (r < 0)
                    MP_FATAL(ctx, "Read error.\n");
                break;
            }
            ctx->line_pos += r;
            // Is there a new line?
            while (1) {
                ctx->line_buf[ctx->line_pos] = '\0';
                char *end = strchr(ctx->line_buf, '\n');
                if (!end)
                    break;
                *end = '\0';
                process_line(ctx, ctx->line_buf);
                int left = ctx->line_pos - (end + 1 - ctx->line_buf);
                memmove(ctx->line_buf, end + 1, left);
                ctx->line_pos = left;
            }
        }
        check_mpv_events(ctx);
    }
    MP_INFO(ctx, "Exiting.\n");
    return 0;
}

static void wakeup_cb(void *p)
{
    struct mp_pipe *pipe = p;
    mp_pipe_interrupt(pipe);
}

static struct slave_client *create_slave(mpv_handle *h, char *fd[2])
{
    const char *cfd[2] = {fd[0], fd[1]};
    for (int n = 0; n < 2; n++) {
        if (!cfd[n])
            cfd[n] = "";
    }
    struct slave_client *ctx = talloc_ptrtype(NULL, ctx);
    *ctx = (struct slave_client){
        .global = mp_client_get_mpctx(h)->global,
        .client = h,
        .pipe = mp_pipe_init(ctx, cfd),
    };
    ctx->log = mp_log_new(ctx, ctx->global->log, "slave");
    if (!ctx->pipe) {
        MP_FATAL(ctx, "Could not open %s", cfd[0]);
        if (cfd[1][0])
            MP_FATAL(ctx, " or %s", cfd[1]);
        MP_FATAL(ctx, ".\n");
        talloc_free(ctx);
        return NULL;
    }
    // Make mp_pipe_poll() leave if there's a new mpv event.
    mpv_set_wakeup_callback(h, wakeup_cb, ctx->pipe);
    return ctx;
}

static void *slave_thread(void *p)
{
    pthread_detach(pthread_self());

    struct slave_client *ctx = p;
    slave_loop(ctx);

    mpv_destroy(ctx->client);
    talloc_free(ctx);
    return NULL;
}

// Start a thread with the client running in it.
int mp_start_slave_client(struct MPContext *mpctx, char *name, char *fd[2])
{
    mpv_handle *h = mp_new_client(mpctx->clients, name);
    struct slave_client *ctx = h ? create_slave(h, fd) : NULL;
    bool ok = false;
    if (ctx) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, slave_thread, ctx) == 0)
            ok = true;
    }
    if (!ctx || !ok) {
        mpv_destroy(h);
        talloc_free(ctx);
        MP_FATAL(mpctx, "Opening slave connection failed.\n");
        return -1;
    }
    return 0;
}

// Run the client (in a blocking manner), and also assume that the client owns
// the underlying player, and that the client handle isn't initialized yet
// (i.e. the client is supposed to invoke mpv_initialize()).
// Return an exit code for exit().
int mp_run_slave_mode(mpv_handle *h, char *fd[2])
{
    struct slave_client *ctx = create_slave(h, fd);
    int r = 0x22;
    if (ctx) {
        slave_loop(ctx);
        talloc_free(ctx);
        r = 0;
    }
    mpv_destroy(h);
    return r;
}
