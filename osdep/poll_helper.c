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

#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "common/common.h"
#include "osdep/io.h"

#include "poll_helper.h"

void mp_poll_uninit(struct mp_poll *p)
{
    if (p->initialized) {
        close(p->wakeup_pipe[0]);
        close(p->wakeup_pipe[1]);
        pthread_mutex_destroy(&p->lock);
    }
    p->initialized = false;
}

int mp_poll_init(struct mp_poll *p)
{
    *p = (struct mp_poll){0};

    if (pipe(p->wakeup_pipe) != 0)
        return -1;
    pthread_mutex_init(&p->lock, NULL);
    p->initialized = true;

    for (int i = 0; i < 2; i++) {
        mp_set_cloexec(p->wakeup_pipe[i]);
        int ret = fcntl(p->wakeup_pipe[i], F_GETFL);
        if (ret == -1)
            goto fail;
        if (fcntl(p->wakeup_pipe[i], F_SETFL, ret | O_NONBLOCK) == -1)
            goto fail;
    }
    return 0;

fail:
    mp_poll_uninit(p);
    return -1;
}

#define NUM_MAX_FD 20

// Call poll() with the given fds/num_fds/timeout arguments. In addition to a
// normal poll(), this also changes the following things:
// - The timeout is in seconds, not milliseconds.
// - Can be interrupted with mp_poll_interrupt().
// - Has a different return value: >=0 on success, <0 and -errno on error.
//   If it was interrupted (and successful), MP_POLL_INTERRUPTED is returned.
// - Handles EINTR automatically.
int mp_poll(struct mp_poll *p, struct pollfd *fds, int num_fds, double timeout)
{
    assert(num_fds <= NUM_MAX_FD - 1);
    struct pollfd p_fds[NUM_MAX_FD];

    memcpy(p_fds, fds, num_fds * sizeof(p_fds[0]));
    p_fds[num_fds] = (struct pollfd){
        .fd = p->wakeup_pipe[0],
        .events = POLLIN,
    };

    pthread_mutex_lock(&p->lock);
    if (p->interrupted) {
        p->interrupted = false;
        pthread_mutex_unlock(&p->lock);
        return MP_POLL_INTERRUPTED;
    }
    p->in_poll = true;
    pthread_mutex_unlock(&p->lock);

    int r;
    do {
        r = poll(p_fds, num_fds + 1, MPMIN(timeout * 1000.0, INT_MAX));
        if (r < 0)
            r = -errno;
    } while (r == -EINTR);

    pthread_mutex_lock(&p->lock);
    p->in_poll = false;
    if (r >= 0) {
        r = p->interrupted ? MP_POLL_INTERRUPTED : 0;
        p->interrupted = false;
    }
    pthread_mutex_unlock(&p->lock);

    memcpy(fds, p_fds, num_fds * sizeof(fds[0]));
    if (p_fds[num_fds].revents & POLLIN) {
        // flush the wakeup pipe contents
        char buf[100];
        read(p->wakeup_pipe[0], buf, sizeof(buf));
    }

    return r;
}

void mp_poll_interrupt(struct mp_poll *p)
{
    // Try to be clever, and don't always write a byte into the wakeup pipe.
    // But the mutex is also needed to avoid a small race condition window,
    // between leaving poll() and emptying the wakeup pipe.
    pthread_mutex_lock(&p->lock);
    bool send_wakeup = p->in_poll && !p->interrupted;
    p->interrupted = true;
    pthread_mutex_unlock(&p->lock);
    if (send_wakeup)
        write(p->wakeup_pipe[1], &(char){0}, 1);
}
