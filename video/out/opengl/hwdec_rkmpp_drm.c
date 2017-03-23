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

#include <libavutil/rational.h>

#include "common/common.h"
#include "common/msg.h"
#include "video/mp_image.h"

#include "hwdec.h"
#include "common.h"

struct priv {
    struct mp_log *log;

    struct mp_image_params params;
  
    int w, h;
    struct mp_rect src, dst;
};

static void overlay_adjust(struct gl_hwdec *hw, int w, int h,
                           struct mp_rect *src, struct mp_rect *dst)
{
    struct priv *p = hw->priv;

    p->w = w;
    p->h = h;
    p->src = *src;
    p->dst = *dst;
}

static int reinit(struct gl_hwdec *hw, struct mp_image_params *params)
{
    struct priv *p = hw->priv;

    p->params = *params;

    *params = (struct mp_image_params){0};

    return 0;
}

static int overlay_frame(struct gl_hwdec *hw, struct mp_image *hw_image)
{
    struct priv *p = hw->priv;

    return 0;
}

static void destroy(struct gl_hwdec *hw)
{
    struct priv *p = hw->priv;
}

static int create(struct gl_hwdec *hw)
{
    struct priv *p = talloc_zero(hw, struct priv);
    hw->priv = p;
    p->log = hw->log;


    return 0;
}

static bool test_format(struct gl_hwdec *hw, int imgfmt)
{
    return imgfmt == IMGFMT_RKMPP;
}

const struct gl_hwdec_driver gl_hwdec_rpi_overlay = {
    .name = "rkmpp-drm",
    .api = HWDEC_RKMPP,
    .test_format = test_format,
    .create = create,
    .reinit = reinit,
    .overlay_frame = overlay_frame,
    .overlay_adjust = overlay_adjust,
    .destroy = destroy,
};
