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

#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "common/common.h"
#include "osdep/io.h"

#include "poll_helper.h"
#include "pipe.h"

struct mp_pipe {
    // 0 is for reading
    // 1 is for writing
    int fd[2];
    bool close_fd[2];
    struct mp_poll poll;
};

static void close_pipe(void *ptr)
{
    struct mp_pipe *p = ptr;
    if (p->fd[0] != -1 && p->close_fd[0])
        close(p->fd[0]);
    if (p->fd[1] != -1 && p->close_fd[1])
        close(p->fd[1]);
    mp_poll_uninit(&p->poll);
}

static int open_fd(struct mp_pipe *p, int n, const char *fd)
{
    if (fd[0]) {
        char *end = (char *)fd;
        p->fd[n] = strtol(fd, &end, 10);
        if (end[0]) {
            int mode = O_CLOEXEC;
            mode |= n ? O_WRONLY : O_RDONLY;
            // Use RDWR for FIFOs to ensure they stay open over multiple accesses.
            if (n == 0) {
                struct stat st;
                if (stat(fd, &st) == 0 && S_ISFIFO(st.st_mode))
                    mode = O_RDWR;
            }
            p->fd[n] = open(fd, mode);
            p->close_fd[n] = true;
        }
        if (p->fd[n] == -1)
            return -1;
    }
    return 0;
}

struct mp_pipe *mp_pipe_init(void *talloc_ctx, const char *fd[2])
{
    struct mp_pipe *p = talloc_ptrtype(talloc_ctx, p);
    *p = (struct mp_pipe) { .fd = { -1, -1 } };
    talloc_set_destructor(p, close_pipe);

    if (mp_poll_init(&p->poll) < 0)
        goto fail;

    if (open_fd(p, 0, fd[0]) < 0)
        goto fail;

    if (open_fd(p, 1, fd[1]) < 0)
        goto fail;

    // Require at least valid reading or writing.
    if (p->fd[0] == -1 && p->fd[1] == -1)
        goto fail;

    return p;
fail:
    talloc_free(p);
    return NULL;
}

ptrdiff_t mp_pipe_read(struct mp_pipe *p, void *buf, size_t count)
{
    if (p->fd[0] == -1)
        return 0;
    ssize_t r = 0;
    do {
        r = read(p->fd[0], buf, count);
    } while (r == -1 && errno == EINTR);
    return r;
}

ptrdiff_t mp_pipe_write(struct mp_pipe *p, void *buf, size_t count)
{
    if (p->fd[1] == -1)
        return 0;
    ssize_t r = 0;
    do {
        r = write(p->fd[1], buf, count);
    } while (r == -1 && errno == EINTR);
    // The pipe was closed, and we signal EOF with 0 bytes written.
    // Normally, this will never happen though, due to SIGPIPE.
    if (r == EPIPE)
        r = 0;
    return r;
}

int mp_pipe_wait(struct mp_pipe *p, int flags)
{
    struct pollfd fds[2] = {
        { .fd = p->fd[0], .events = (flags & MP_PIPE_READ) ? POLLIN : 0 },
        { .fd = p->fd[1], .events = (flags & MP_PIPE_WRITE) ? POLLOUT : 0 },
    };
    int r = mp_poll(&p->poll, fds, 2, -1);
    int res = 0;
    if (r < 0)
        return r;
    if (r & MP_POLL_INTERRUPTED)
        res |= MP_PIPE_INTERRUPTED;
    printf("got ev: %d %d \n", fds[0].revents, fds[1].revents);
    for (int n = 0; n < 2; n++) {
        if (fds[n].revents & (POLLERR | POLLHUP)) {
            if (p->close_fd[n])
                close(p->fd[n]);
            p->fd[n] = -1;
            fds[n].revents |= POLLIN | POLLOUT; // make code below set the flags
        }
    }
    if (fds[0].revents & POLLIN)
        res |= MP_PIPE_READ;
    if (fds[1].revents & POLLOUT)
        res |= MP_PIPE_WRITE;
    return res;
}

void mp_pipe_interrupt(struct mp_pipe *p)
{
    mp_poll_interrupt(&p->poll);
}
