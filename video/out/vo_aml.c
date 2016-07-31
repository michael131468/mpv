/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <amcodec/codec.h>
#include <libavcodec/aml.h>

#include "common/common.h"
#include "common/msg.h"
#include "video/mp_image.h"

#include "vo.h"

// amvideo freerun Frame ioctls
#define AMV_DEVICE_NAME   "/dev/amvideo"

#define _A_M  'S'
#define AMSTREAM_IOC_GET_FREERUN_FRAME _IOW((_A_M), 0xb1, unsigned long)
#define AMSTREAM_IOC_PUT_FREERUN_FRAME _IOW((_A_M), 0xb2, unsigned long)
#define AMSTREAM_IOC_FREE_FREERUN_FRAME _IOW((_A_M), 0xb3, unsigned long)

#define DEBUG (0)

typedef struct priv {
   int fd;
} aml_private;


static int amlsysfs_write_int(struct vo *vo, const char *path, int value)
{
    int ret = 0;
    char cmd[64];
    int fd = open(path, O_RDWR, 0644);
    if (fd >= 0)
    {
      snprintf(cmd, sizeof(cmd), "%d", value);

      ret = write(fd, cmd, strlen(cmd));
      if (ret < 0)
        MP_ERR(vo, "failed to set %s to %d\n", path, value);
      close(fd);
      return 0;
    }
  return -1;
}

static int query_format(struct vo *vo, int format)
{
    return (format == IMGFMT_AML);
}

static void flip_page(struct vo *vo)
{
}

static void draw_frame(struct vo *vo, struct vo_frame *frame)
{
  int ret;
  aml_private *priv = (aml_private*)vo->priv;

  // enable video layer
  amlsysfs_write_int(vo, "/sys/class/video/disable_video", 0);

  if (!frame->current)
    return;

  // get the buffer structure from plane[0]
  AMLBuffer *buffer = (AMLBuffer*)frame->current->planes[0];
  if (!buffer)
    return;

#ifdef DEBUG
  MP_VERBOSE(vo, "drawing frame with handle=%ld\n", buffer->handle);
#endif

  // Putting the frame will free it in kernel, so reset this flag
  buffer->free = 0;

  ret = ioctl(priv->fd, AMSTREAM_IOC_PUT_FREERUN_FRAME, &buffer->handle);
  if (ret)
  {
    MP_ERR(vo, "ioctl for AMSTREAM_IOC_PUT_FREERUN_FRAME failed with code %d\n", ret);
    return;
  }
}

static int reconfig(struct vo *vo, struct mp_image_params *params)
{
  return 0;
}

static int control(struct vo *vo, uint32_t request, void *data)
{
  return VO_NOTIMPL;
}

static void uninit(struct vo *vo)
{
  aml_private *priv = (aml_private*)vo->priv;

  // disable video output
  amlsysfs_write_int(vo, "/sys/class/video/disable_video", 1);

  // close amvideo driver
  if (priv->fd)
  {
    MP_VERBOSE(vo, "closed device %s with fd=%d\n", AMV_DEVICE_NAME, priv->fd);
    close(priv->fd);
  }

}

static int preinit(struct vo *vo)
{
  aml_private *priv = (aml_private*)vo->priv;

  // disable video output
  amlsysfs_write_int(vo, "/sys/class/video/disable_video", 1);

  // open the ion device
  if ((priv->fd = open(AMV_DEVICE_NAME, O_RDWR)) < 0)
  {
    MP_ERR(vo, "Failed to open %s\n", AMV_DEVICE_NAME);
    return -1;
  }

  MP_VERBOSE(vo, "openned %s with fd=%d\n", AMV_DEVICE_NAME, priv->fd);

  return 0;
}

const struct vo_driver video_out_aml = {
    .description = "Amlogic (Amcodec)",
    .name = "aml",
    .preinit = preinit,
    .reconfig = reconfig,
    .query_format = query_format,
    .control = control,
    .draw_frame = draw_frame,
    .flip_page = flip_page,
    .uninit = uninit,
    .priv_size = sizeof(struct priv),
};

