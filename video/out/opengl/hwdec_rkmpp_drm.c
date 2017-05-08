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

#include <assert.h>
#include <libavcodec/drmprime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "common.h"
#include "hwdec.h"
#include "common/msg.h"
#include "options/m_config.h"
#include "video/out/drm_common.h"
#include "video/mp_image.h"

struct priv {
    struct mp_log *log;

    struct mp_image_params params;
  
    struct kms *kms;
    uint32_t current_fbid, old_fbid;

    struct mp_image *current_frame, *old_frame;

    int w, h;
    struct mp_rect src, dst;
};

static void remove_overlay(struct gl_hwdec *hw, int fb_id)
{
    struct priv *p = hw->priv;

    if (fb_id)
        drmModeRmFB(p->kms->fd, fb_id);
}

static int reinit(struct gl_hwdec *hw, struct mp_image_params *params)
{
    struct priv *p = hw->priv;

    p->params = *params;
    *params = (struct mp_image_params){0};

    remove_overlay(hw, p->current_fbid);
    p->current_fbid = 0;

    mp_image_setrefp(&p->current_frame, NULL);
    return 0;
}

static int overlay_frame(struct gl_hwdec *hw, struct mp_image *hw_image)
{
    struct priv *p = hw->priv;
    uint32_t gem_handle;
    av_drmprime *primedata = NULL;
    int ret;
    uint32_t pitches[4] = { 0, 0, 0, 0};
    uint32_t offsets[4] = { 0, 0, 0, 0};
    uint32_t handles[4] = { 0, 0, 0, 0};
    uint32_t fb_id = 0;

    if (hw_image) {
        primedata = (av_drmprime *)hw_image->planes[3];

        if (primedata) {
            ret = drmPrimeFDToHandle(p->kms->fd, primedata->fds[0], &gem_handle);
            if (ret < 0) {
                MP_ERR(p, "Failed to retrieve the Prime Handle.\n");
                goto err;
            }

            pitches[0] = primedata->strides[0] * 2;
            offsets[0] = primedata->offsets[0];
            handles[0] = gem_handle;
            pitches[1] = primedata->strides[1] * 2;
            offsets[1] = primedata->offsets[1];
            handles[1] = gem_handle;

            int srcw = p->src.x1 - p->src.x0;
            int srch = p->src.y1 - p->src.y0;
            int dstw = MP_ALIGN_UP(p->dst.x1 - p->dst.x0, 16);
            int dsth = MP_ALIGN_UP(p->dst.y1 - p->dst.y0, 16);


            ret = drmModeAddFB2(p->kms->fd, hw_image->w, hw_image->h /2 , primedata->format,
                                handles, pitches, offsets, &fb_id, 0);

            if (ret < 0) {
                MP_ERR(p, "Failed to add drm layer %d.\n", fb_id);
                goto err;
            }

            ret = drmModeSetPlane(p->kms->fd, p->kms->plane_id, p->kms->crtc_id, fb_id, 0,
                                  0, 0, dstw, dsth,
                                  p->src.x0 << 16, p->src.y0 << 16 , srcw << 16, srch << 15);
            if (ret < 0) {
                MP_ERR(p, "Failed to set the plane %d (buffer %d).\n", p->kms->plane_id,
                            fb_id);
                goto err;
            }

            remove_overlay(hw, p->current_fbid);
            p->current_fbid = fb_id;
            mp_image_setrefp(&p->old_frame, p->current_frame);
            mp_image_setrefp(&p->current_frame, hw_image);
        }
    }
    else {
        remove_overlay(hw, p->current_fbid);
        p->current_fbid = 0;
        mp_image_setrefp(&p->current_frame, hw_image);
        mp_image_setrefp(&p->old_frame, hw_image);
    }



    return 0;
    
 err:
   return ret;
}

static void overlay_adjust(struct gl_hwdec *hw, int w, int h,
                           struct mp_rect *src, struct mp_rect *dst)
{
    struct priv *p = hw->priv;
    drmModeCrtcPtr crtc;
    double hratio, vratio;

    p->w = w;
    p->h = h;
    p->src = *src;
    p->dst = *dst;

    // drm can allow to have a layer that has a different size from framebuffer
    // we scale here the destination size to video mode
    hratio = vratio = 1.0;
    crtc = drmModeGetCrtc(p->kms->fd, p->kms->crtc_id);
    if (crtc) {
        hratio = (crtc->mode.hdisplay / (p->dst.x1 - p->dst.x0));
        vratio = (crtc->mode.vdisplay / (p->dst.y1 - p->dst.y0));
        drmModeFreeCrtc(crtc);
    }

    p->dst.x0 *= hratio;
    p->dst.x1 *= hratio;
    p->dst.y0 *= vratio;
    p->dst.y1 *= vratio;

    overlay_frame(hw, p->current_frame);
}

static void destroy(struct gl_hwdec *hw)
{
    struct priv *p = hw->priv;

    remove_overlay(hw, p->current_fbid);
    p->current_fbid = 0;

    mp_image_setrefp(&p->current_frame, NULL);

    if (p->kms) {
        kms_destroy(p->kms);
        p->kms = NULL;
    }
}

static int create(struct gl_hwdec *hw)
{
    struct priv *p = talloc_zero(hw, struct priv);
    hw->priv = p;
    p->log = hw->log;

    char *connector_spec;
    int drm_mode, drm_layer;

    mp_read_option_raw(hw->global, "drm-connector", &m_option_type_string, &connector_spec);
    mp_read_option_raw(hw->global, "drm-mode", &m_option_type_int, &drm_mode);
    mp_read_option_raw(hw->global, "drm-layer", &m_option_type_int, &drm_layer);

    talloc_free(connector_spec);

    p->kms = kms_create(hw->log, connector_spec, drm_mode, drm_layer);
    if (!p->kms) {
        MP_ERR(p, "Failed to create KMS.\n");
        goto err;
    }

    uint64_t has_prime;
    if (drmGetCap(p->kms->fd, DRM_CAP_PRIME, &has_prime) < 0) {
        MP_ERR(p, "Card \"%d\" does not support prime handles.\n",
               p->kms->card_no);
        goto err;
    }

    return 0;

err:
    destroy(hw);
    return -1;
}

static bool test_format(struct gl_hwdec *hw, int imgfmt)
{
    return imgfmt == IMGFMT_RKMPP;
}

const struct gl_hwdec_driver gl_hwdec_rkmpp_drm = {
    .name = "rkmpp-drm",
    .api = HWDEC_RKMPP,
    .test_format = test_format,
    .create = create,
    .reinit = reinit,
    .overlay_frame = overlay_frame,
    .overlay_adjust = overlay_adjust,
    .destroy = destroy,
};
