#include "pipe.h"

struct mp_pipe *mp_pipe_init(void *talloc_ctx, const char *fd[2])
{
    return NULL;
}

ptrdiff_t mp_pipe_read(struct mp_pipe *p, void *buf, size_t count)
{
    return -1;
}

ptrdiff_t mp_pipe_write(struct mp_pipe *p, void *buf, size_t count)
{
    return -1;
}

int mp_pipe_wait(struct mp_pipe *p, int flags)
{
    return -1;
}

void mp_pipe_interrupt(struct mp_pipe *p)
{
}
