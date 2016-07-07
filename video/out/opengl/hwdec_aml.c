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

#include <stddef.h>
#include <assert.h>

#include "EGL/egl.h"
#include <EGL/eglext.h>

#include <drm/drm_fourcc.h>
#include "video/img_fourcc.h"
#include <libavcodec/aml.h>

#include "hwdec.h"
#include "utils.h"
#include "video/aml.h"

#define EGL_LINUX_DMA_BUF_EXT          0x3270

#define EGL_LINUX_DRM_FOURCC_EXT        0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT       0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT   0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT    0x3274
#define EGL_DMA_BUF_PLANE1_FD_EXT       0x3275
#define EGL_DMA_BUF_PLANE1_OFFSET_EXT   0x3276
#define EGL_DMA_BUF_PLANE1_PITCH_EXT    0x3277
#define EGL_DMA_BUF_PLANE2_FD_EXT       0x3278
#define EGL_DMA_BUF_PLANE2_OFFSET_EXT   0x3279
#define EGL_DMA_BUF_PLANE2_PITCH_EXT    0x327A
#define EGL_YUV_COLOR_SPACE_HINT_EXT    0x327B
#define EGL_SAMPLE_RANGE_HINT_EXT       0x327C
#define EGL_YUV_CHROMA_HORIZONTAL_SITING_HINT_EXT  0x327D
#define EGL_YUV_CHROMA_VERTICAL_SITING_HINT_EXT    0x327E

#define EGL_ITU_REC601_EXT   0x327F
#define EGL_ITU_REC709_EXT   0x3280
#define EGL_ITU_REC2020_EXT  0x3281

#define EGL_YUV_FULL_RANGE_EXT    0x3282
#define EGL_YUV_NARROW_RANGE_EXT  0x3283

#define EGL_YUV_CHROMA_SITING_0_EXT    0x3284
#define EGL_YUV_CHROMA_SITING_0_5_EXT  0x3285

#define NUM_PLANES  1

static int reinit(struct gl_hwdec *hw, struct mp_image_params *params);
static void unmap_frame(struct gl_hwdec *hw);

struct priv {
    struct mp_log *log;
    struct mp_aml_ctx *ctx;
    //GLuint gl_textures[NUM_PLANES];
    GLuint gl_texture;

    AMLBuffer *current_buffer;
    //EGLImageKHR images[NUM_PLANES];
    EGLImageKHR image;
    int current_fd;
    EGLImageKHR (EGLAPIENTRY *CreateImageKHR)(EGLDisplay, EGLContext,
                                              EGLenum, EGLClientBuffer,
                                              const EGLint *);
    EGLBoolean (EGLAPIENTRY *DestroyImageKHR)(EGLDisplay, EGLImageKHR);
    void (EGLAPIENTRY *EGLImageTargetTexture2DOES)(GLenum, GLeglImageOES);
};


static void destroy_textures(struct gl_hwdec *hw)
{
  struct priv *p = hw->priv;
  GL *gl = hw->gl;

  if (p->gl_texture)
     gl->DeleteTextures(1, &p->gl_texture);
  p->gl_texture = 0;

  if (p->image)
    p->DestroyImageKHR(eglGetCurrentDisplay(), p->image);
  p->image = 0;


  //  for (int i=0; i < NUM_PLANES; i++)
  //  {
  //    if (p->gl_textures[i])
  //    {
  //      gl->DeleteTextures(1, &p->gl_textures[i]);
  //      p->gl_textures[i] = 0;
  //    }

  //    if (p->images[i])
  //      p->DestroyImageKHR(eglGetCurrentDisplay(), p->images[i]);
  //    p->images[i] = 0;

  //  }
}

static void destroy(struct gl_hwdec *hw)
{
    destroy_textures(hw);
}

static int create(struct gl_hwdec *hw)
{
    struct priv *p = talloc_zero(hw, struct priv);
    hw->priv = p;
    p->log = hw->log;

    // check if we have the right EGL extensions
    const char *exts = eglQueryString(eglGetCurrentDisplay(), EGL_EXTENSIONS);

    if (!exts)
    {
      MP_ERR(p, "EGL extensions are empty !\n");
      return -1;
    }

    if (!strstr(exts, "EGL_EXT_image_dma_buf_import"))
    {
      MP_ERR(p, "EGL_EXT_image_dma_buf_import not found.\n");
      return -1;
    }

    p->gl_texture = 0;
    p->image = 0;
    p->current_buffer = NULL;

    p->ctx = talloc_ptrtype(NULL, p->ctx);
    *p->ctx = (struct mp_aml_ctx) {
        .log = hw->log,
        .hwctx = {
            .type = HWDEC_AML,
            .ctx = p->ctx,
        },
    };

    if (!p->ctx)
        return -1;

    static const char *es2_exts[] = {"GL_OES_EGL_image_external ", 0};

    hw->glsl_extensions = es2_exts;

    p->ctx->hwctx.driver_name = hw->driver->name;
    hwdec_devices_add(hw->devs, &p->ctx->hwctx);

    return 0;
}

static int reinit(struct gl_hwdec *hw, struct mp_image_params *params)
{
    struct priv *p = hw->priv;

    // free existing textures if any
    unmap_frame(hw);

    // we grab the missing GL / EGL entry points
    // EGL_KHR_image_base
    p->CreateImageKHR = (void *)eglGetProcAddress("eglCreateImageKHR");
    p->DestroyImageKHR = (void *)eglGetProcAddress("eglDestroyImageKHR");

    // GL_OES_EGL_image
    p->EGLImageTargetTexture2DOES =
        (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if (!p->CreateImageKHR || !p->DestroyImageKHR ||
        !p->EGLImageTargetTexture2DOES)
        return -1;

    params->imgfmt = IMGFMT_RGB0;
    return 0;
}

static void unmap_frame(struct gl_hwdec *hw)
{
  struct priv *p = hw->priv;

  MP_VERBOSE(p, "unmap_frame called for fd=%d\n", p->current_fd);
//  if (p->image)
//    p->DestroyImageKHR(eglGetCurrentDisplay(), p->image);
//  p->image = 0;

//  if (p->current_buffer)
//    p->current_buffer->requeue=1;

  destroy_textures(hw);
}

static int map_frame(struct gl_hwdec *hw, struct mp_image *hw_image,
                     struct gl_hwdec_frame *out_frame)
{
    struct priv *p = hw->priv;
    GL *gl = hw->gl;
    AMLBuffer *pbuffer = (AMLBuffer*)hw_image->planes[0];

    MP_VERBOSE(p, "map_frame called with dmabuf fd=%d, pts=%f, (w=%d, h=%d, stride=%d, index=%d)\n",
               pbuffer->fd_handle, pbuffer->fpts, hw_image->w, hw_image->h, hw_image->stride[0], pbuffer->index);

    //destroy_textures(hw);

    // If we have a new buffer, make sure we requeue the previous one
//    if ((p->current_buffer) && (p->current_buffer != pbuffer))
//    {
//      MP_VERBOSE(p, "Flag Buffer #%d for requeue", p->current_buffer->index);
//      p->current_buffer->requeue = 1;
//    }

    GLenum gltarget = GL_TEXTURE_EXTERNAL_OES;

    gl->GenTextures(1, &p->gl_texture);
    gl->BindTexture(gltarget, p->gl_texture);
    gl->TexParameteri(gltarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(gltarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(gltarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(gltarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->BindTexture(gltarget, 0);

    if (pbuffer->fd_handle)
    {
      const EGLint img_attrs[] = {
          EGL_WIDTH, pbuffer->width,
          EGL_HEIGHT, pbuffer->height,
          EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_NV21,
          EGL_DMA_BUF_PLANE0_FD_EXT,	pbuffer->fd_handle,
          EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
          EGL_DMA_BUF_PLANE0_PITCH_EXT, pbuffer->stride,
          EGL_DMA_BUF_PLANE1_FD_EXT,	pbuffer->fd_handle,
          EGL_DMA_BUF_PLANE1_OFFSET_EXT, pbuffer->stride * pbuffer->height,
          EGL_DMA_BUF_PLANE1_PITCH_EXT, pbuffer->stride,
          EGL_YUV_COLOR_SPACE_HINT_EXT, EGL_ITU_REC709_EXT,
          EGL_SAMPLE_RANGE_HINT_EXT, EGL_YUV_FULL_RANGE_EXT,
          EGL_NONE
        };

      p->image = p->CreateImageKHR(eglGetCurrentDisplay(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0, img_attrs);
      if (!p->image)
      {
        MP_ERR(p,"CreateImageKHR error 0x%x\n", eglGetError());
        goto err;
      }

      gl->BindTexture(GL_TEXTURE_EXTERNAL_OES, p->gl_texture);
      p->EGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, p->image);
      p->current_fd = pbuffer->fd_handle;
      p->current_buffer = pbuffer;
    }

    out_frame->planes[0] = (struct gl_hwdec_plane){
        .gl_texture = p->gl_texture,
        .gl_target = gltarget,
        .tex_w =  hw_image->w,
        .tex_h = hw_image->h,
    };

    gl->BindTexture(GL_TEXTURE_2D, 0);

    return 0;

  err:
    MP_FATAL(p, "mapping AML GLES image failed\n");
    return -1;
}

const struct gl_hwdec_driver gl_hwdec_aml = {
    .name = "aml-gles",
    .api = HWDEC_AML,
    .imgfmt = IMGFMT_AML,
    .create = create,
    .reinit = reinit,
    .map_frame = map_frame,
    .unmap = unmap_frame,
    .destroy = destroy,
};
