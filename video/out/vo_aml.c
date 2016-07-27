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
#include <amcodec/amports/amstream.h>

#include "common/common.h"
#include "common/msg.h"
#include "video/mp_image.h"
#include "vo.h"

#define TRICKMODE_NONE  0x00
#define TRICKMODE_I     0x01
#define TRICKMODE_FFFB  0x02

// IOCTL defines
#define VFM_GRABBER_GET_FRAME   _IOWR('V', 0x01, vfm_grabber_frame)
#define VFM_GRABBER_GET_INFO    _IOWR('V', 0x02, vfm_grabber_info)
#define VFM_GRABBER_PUT_FRAME   _IOWR('V', 0x03, vfm_grabber_frame)

#define _A_M  'S'
#define AMSTREAM_IOC_SET_FREERUN_PTS  _IOW((_A_M), 0x5c, int)

#define VFM_DEVICE_NAME     "/dev/vfm_grabber"
#define AMVIDEO_DEVICE_NAME "/dev/video10"

typedef struct
{
  int dma_fd;
  int width;
  int height;
  int stride;
  void *priv;
} vfm_grabber_frame;

typedef struct priv {
   double mpv_start_pts;
   int vfm_fd;
   int amv_fd;
} aml_private;

typedef struct
{
  double pts;
} AMLFramePrivate;

void *vtop(int vaddr);

static int amlsysfs_write_string(struct vo *vo, const char *path, const char *value)
{
    int ret = 0;
    int fd = open(path, O_RDWR, 0644);
    if (fd >= 0)
    {
      ret = write(fd, value, strlen(value));
      if (ret < 0)
        MP_ERR(vo, "failed to set %s to %s\n", path, value);
      close(fd);
      return 0;
    }
  return -1;
}

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

static int64_t amlsysfs_read_int(struct vo *vo, const char *path, int base)
{
    char val[16];
    int fd = open(path, O_RDWR, 0644);
    if (fd >= 0)
    {
      if (read(fd, val, sizeof(val)) < 0)
        MP_ERR(vo, "failed to read %s\n", path);
      close(fd);

      return strtoul(val, NULL, base);
    }
  return -1;
}


static int query_format(struct vo *vo, int format)
{
  MP_VERBOSE(vo, "query_format %d / %d)\n", format, IMGFMT_AML);
    return (format == IMGFMT_AML);
}

static void flip_page(struct vo *vo)
{
  //MP_VERBOSE(vo, "flip_page\n");
}

#define USE_VFM 0
#define USE_AMV 1

static void draw_frame(struct vo *vo, struct vo_frame *frame)
{
  aml_private *priv = (aml_private*)vo->priv;
  int ret;

#if USE_VFM
  vfm_grabber_frame *vfm_frame;

  MP_VERBOSE(vo, "draw_frame called\n");
  if (!frame->current)
    return;

  vfm_frame = (vfm_grabber_frame *)frame->current->planes[0];

  MP_VERBOSE(vo, "draw_frame called on %dx%d image\n", vfm_frame->width, vfm_frame->height);
  if (priv->vfm_fd)
  {
    ret = ioctl(priv->vfm_fd, VFM_GRABBER_PUT_FRAME, vfm_frame);
    if (ret > 0)
    {
      MP_ERR(vo, "VFM_GRABBER_PUT_FRAME ictl failed (code=%d)\n", ret);
      return;
     }
  }
  MP_VERBOSE(vo, "draw_frame\n");
#endif

#if USE_AMV
  if (!frame->current)
    return;

   int current = amlsysfs_read_int(vo, "/sys/module/amvideo/parameters/freerun_frameid", 10);
   MP_VERBOSE(vo, "draw_frame id=%d/%d\n", frame->current->planes[0], current);
   amlsysfs_write_int(vo,"/sys/module/amvideo/parameters/freerun_frameid", frame->current->planes[0]);

//  if (priv->amv_fd)
//  {
//    int pts = 90000;
//    ret = ioctl(priv->amv_fd, AMSTREAM_IOC_SET_FREERUN_PTS, pts);
//    if (ret > 0)
//    {
//      MP_ERR(vo, "AMSTREAM_IOC_SET_FREERUN_PTS ictl failed (code=%d)\n", ret);
//      return;
//     }
//  }
  MP_VERBOSE(vo, "draw_frame\n");
#endif
  return;
//  // do what we need on first required frame
//  if (!priv->mpv_start_pts)
//  {
//    priv->mpv_start_pts = frame->pts / 1000000.0;
//    amlsysfs_write_int(vo, "/sys/class/video/disable_video", 0);
//  }

  if (frame->current)
  {
    codec_para_t *pcodec = (codec_para_t*)frame->current->planes[0];
    AMLFramePrivate *fp = (AMLFramePrivate *)frame->current->planes[1];

    double packet_pts = fp->pts;
    double video_pts = (double)codec_get_vpts(pcodec) / 90000.0;
    double pcrscr_pts =  (double)codec_get_pcrscr(pcodec) / 90000.0;
    double mpv_pts = frame->pts / 1000000.0;
    double error_pcrpts = packet_pts - pcrscr_pts;
    double error_vidpts = packet_pts - video_pts;
    double error_pts = error_vidpts;

#define PTS_CORRECTION 0.3

    //return;

    MP_VERBOSE(vo, "draw_frame with pts = %f, packet pts=%f, video pts=%f, pcrscr_pts=%f (pcrerr=%f) (viderr=%f)\n", mpv_pts, packet_pts, video_pts, pcrscr_pts, error_pcrpts, error_vidpts);


    if (fabs(error_pcrpts) > 0.1)
    {
      double vid_pcr_shift = video_pts - pcrscr_pts;
      double corrected_pts = pcrscr_pts - error_vidpts;
      corrected_pts = packet_pts;

      // enable free run mode for pts correction
      amlsysfs_write_int(vo, "/sys/class/video/freerun_mode", 1);

      MP_VERBOSE(vo, "shifting pcr pts to %f\n", corrected_pts);
      //codec_pause(pcodec);
      char strvalue[32];
      sprintf(strvalue, "0x%x", corrected_pts * 90000);
      amlsysfs_write_string(vo,"/sys/class/tsync/pts_pcrsrc", strvalue);
      amlsysfs_write_string(vo,"/sys/class/tsync/pts_video", strvalue);
      codec_set_pcrscr(pcodec, corrected_pts * 90000);
      codec_set_video_delay_limited_ms(pcodec, 100);
      //codec_set_cntl_mode(pcodec, TRICKMODE_FFFB);
      //codec_resume(pcodec);


//        double corrected_pts;
//        corrected_pts = packet_pts + 0.1 ;

//        for (int i=0; i < 20; i++)
//        {
//          int ret = codec_set_pcrscr(pcodec, corrected_pts * 90000);
//          usleep(1000);
//        }

        //MP_VERBOSE(vo, "shifting pcr pts to %f\n", corrected_pts);
    }
    else
    {
      // disable free run mode for smooth playback
      //amlsysfs_write_int(vo, "/sys/class/video/freerun_mode", 0);

    }

    if (fabs(error_vidpts) > 0.1)
    {
      double corrected_pts;
      corrected_pts = packet_pts ;

      //codec_set_cntl_mode(pcodec, TRICKMODE_I);

    }


    return;
    if (fabs(error_pts) > 0.1)
    {
      // acceptable error, we'll resync slowly
      double corrected_pts;

      if (error_pts > 0)
      {
        // we're late, we need to move our pcsscr forward
        corrected_pts = pcrscr_pts + fmin(PTS_CORRECTION, fabs(error_pts));
        corrected_pts = fmin(corrected_pts, packet_pts);
      }
      else
      {
        // we're in advance
        corrected_pts = pcrscr_pts - fmin(PTS_CORRECTION, fabs(error_pts));
        corrected_pts = fmax(corrected_pts, packet_pts);
      }

      //corrected_pts = packet_pts;
      //codec_pause(pcodec);
      MP_VERBOSE(vo, "shifting pts to %f\n", corrected_pts);

      int ret = codec_set_pcrscr(pcodec, corrected_pts * 90000);
      if (ret < 0)
      {
        MP_ERR(vo, "failed to set pcrscr_pts");
      }

      char strvalue[32];
      sprintf(strvalue, "0x%x", (int)(corrected_pts * 90000.0));
      amlsysfs_write_string(vo,"/sys/class/tsync/pts_video", strvalue);
#if 0
      char strvalue[32];
      sprintf(strvalue, "0x%x", (int)(corrected_pts * 90000.0));
      int ret = codec_set_pcrscr(pcodec, corrected_pts * 90000);
      if (ret < 0)
      {
        MP_ERR(vo, "failed to set pcrscr_pts");
      }
      pcrscr_pts =  (double)codec_get_pcrscr(pcodec) / 90000.0;
      if (pcrscr_pts != corrected_pts)
      {
        MP_ERR(vo, "failed to set pcrscr_pts %f -> %f", corrected_pts, pcrscr_pts);
      }
      //codec_set_pcrscr(pcodec, (int)(packet_pts * 90000))
      //amlsysfs_write_string(vo,"/sys/class/tsync/pts_pcrscr", strvalue);
      amlsysfs_write_string(vo,"/sys/class/tsync/pts_video","0x0");
      //codec_resume(pcodec);
#endif

    }
  }
}

static int reconfig(struct vo *vo, struct mp_image_params *params)
{
  return 0;
}


static int control(struct vo *vo, uint32_t request, void *data)
{
  //struct priv *p = vo->priv;

  return VO_NOTIMPL;
}

static void uninit(struct vo *vo)
{
  aml_private *priv = (aml_private*)vo->priv;
  // clear framebuffer
//  amlsysfs_write_int(vo, "/sys/class/graphics/fb1/blank", 0);
//  amlsysfs_write_int(vo, "/sys/class/graphics/fb0/blank", 1);

  // restore tsync
  amlsysfs_write_int(vo, "/sys/class/tsync/enable", 1);

  // disable video output
  amlsysfs_write_int(vo, "/sys/class/video/disable_video", 1);

#if USE_VFM
  if (priv->vfm_fd)
  {
    close(priv->vfm_fd);
  }
#endif

#if USE_AMV
  if (priv->amv_fd)
  {
    close(priv->amv_fd);
  }
#endif

}

static int preinit(struct vo *vo)
{
  aml_private *priv = (aml_private*)vo->priv;

  // clear framebuffer
//  amlsysfs_write_int(vo, "/sys/class/graphics/fb1/blank", 0);
  //amlsysfs_write_int(vo, "/sys/class/graphics/fb0/blank", 1);

  // disable tsync
//  amlsysfs_write_int(vo, "/sys/class/tsync/enable", 0);
//  amlsysfs_write_int(vo, "/sys/class/tsync/mode", 0);

  // disable blackout policy
  amlsysfs_write_int(vo, "/sys/class/video/blackout_policy", 0);

  // disable video output
  amlsysfs_write_int(vo, "/sys/class/video/disable_video", 1);
  amlsysfs_write_int(vo, "/sys/class/video/freerun_mode", 1);

  //amlsysfs_write_string(vo,"/sys/class/tsync/pts_video", "0x0");

//  amlsysfs_write_int(vo, "/sys/class/video/screen_mode", 1);

  // set video size
  //amlsysfs_write_string(vo, "/sys/class/video/axis", "0 0 1920 1080");

  priv->mpv_start_pts = 0;

#if USE_VFM
  priv->vfm_fd = 0;

  // open the ion device
  if ((priv->vfm_fd = open(VFM_DEVICE_NAME, O_RDWR)) < 0)
  {
    MP_ERR(vo, "Failed to open %s\n", VFM_DEVICE_NAME);
    return -1;
  }

  MP_VERBOSE(vo, "Initialization successful\n");
#endif

#if USE_AMV
  priv->amv_fd = 0;

  // open the ion device
  if ((priv->amv_fd = open(AMVIDEO_DEVICE_NAME, O_RDWR)) < 0)
  {
    MP_ERR(vo, "Failed to open %s\n", AMVIDEO_DEVICE_NAME);
    return -1;
  }

  MP_VERBOSE(vo, "Initialization successful\n");
#endif

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

