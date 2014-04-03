#ifndef MP_SLAVE_MODE_H_
#define MP_SLAVE_MODE_H_

struct mpv_handle;
struct MPContext;
int mp_run_slave_mode(struct mpv_handle *h, char *fd[2]);
int mp_start_slave_client(struct MPContext *mpctx, char *name, char *fd[2]);

#endif
