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

#ifndef MP_POLL_HELPER_H_
#define MP_POLL_HELPER_H_

#include <stdbool.h>
#include <pthread.h>
#include <poll.h>

// helper to setup and maintain the wakeup pipe
struct mp_poll {
    bool initialized, in_poll, interrupted;
    int wakeup_pipe[2];
    pthread_mutex_t lock;
};

#define MP_POLL_INTERRUPTED 0x100

int mp_poll_init(struct mp_poll *p);
void mp_poll_uninit(struct mp_poll *p);
int mp_poll(struct mp_poll *p, struct pollfd *fds, int num_fds, double timeout);
void mp_poll_interrupt(struct mp_poll *p);

#endif
