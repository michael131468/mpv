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

#ifndef MP_PIPE_H_
#define MP_PIPE_H_

#include <stddef.h>

// Abstracts reading/writing from/to a pipe in a non-blocking way.
// Because MS Windows is terrible.
struct mp_pipe;

// Create mp_pipe, with fd[0] being the file or UNIX file descriptor for
// reading, and fd[1] for writing.
// Note that fd[x] can be a filename (this again is because of Windows - if it
// turns out that windows in fact does not need special methods to open a
// named pipe, this could be reduced to int fd[2]).
// fd[1] can be "", in which case fd[0] is assumed to be either bidirectional
// or write-only.
struct mp_pipe *mp_pipe_init(void *talloc_ctx, const char *fd[2]);

// Read some data from fd[0]. As long as the pipe is open and count>0, at least
// one byte must be read by the function. If there's no new data yet, the call
// blocks (until the other end is written or closed).
// If EOF is reached (i.e. the pipe has been closed and all buffered data was
// already read), 0 is returned.
// On error, a negative number is returned.
// This function blocks, unless mp_pipe_select() returned that it doesn't.
ptrdiff_t mp_pipe_read(struct mp_pipe *p, void *buf, size_t count);

// Write some data to fd[1]. As long as the pipe is open and count>0, at least
// one byte must be written by the function. If the pipe is full, the call
// blocks (until the other end is read or closed).
// If the pipe is closed, 0 is returned.
// On error, a negative number is returned.
// This function blocks, unless mp_pipe_select() returned that it doesn't.
ptrdiff_t mp_pipe_write(struct mp_pipe *p, void *buf, size_t count);

#define MP_PIPE_READ 1
#define MP_PIPE_WRITE 2
#define MP_PIPE_INTERRUPTED 32

// flags is a set of bits, with MP_PIPE_READ or MP_PIPE_WRITE set. Calling
// this function will wait until one of the following happens:
// - one of the actions specified in flags becomes possible, e.g. if
//   mp_pipe_read() can be called without blocking, MP_PIPE_READ will be set
// - it was interrupted with mp_pipe_interrupt(), then MP_PIPE_INTERRUPTED
//   will be set (this can happen simultaneously with MP_PIPE_READ/WRITE, so
//   check for the bits instead of comparing the whole value)
// - an error happens (then a negative error code is returned)
// Returns:
//  >= 0: bitmask of MP_PIPE_READ, MP_PIPE_WRITE, MP_PIPE_INTERRUPTED
//  <  0: (unspecified) error code
int mp_pipe_wait(struct mp_pipe *p, int flags);

// Interrupt a mp_pipe_select() operation. If mp_pipe_select() is currently not
// being called, the next call must be interrupted.
// This must not interrupt ongoing mp_pipe_read() or mp_pipe_write() operations.
// mp_pipe_select() doesn't necessarily return MP_PIPE_INTERRUPTED for each
// time it has been interrupted - as long as it returns in any way, and the
// caller of mp_pipe_select() can check the condition without running into a
// race condition.
// This function must be thread-safe.
void mp_pipe_interrupt(struct mp_pipe *p);

#endif
