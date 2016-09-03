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

#include <libavcodec/avcodec.h>
#include <libavutil/common.h>

#include "lavc.h"
#include "common/common.h"
#include "video/aml.h"
#include "video/hwdec.h"

struct priv {
    struct mp_log              *log;
    struct mp_aml_ctx          *mpaml;
};

static int init_decoder(struct lavc_ctx *ctx, int w, int h)
{
  return 0;
}

static void uninit(struct lavc_ctx *ctx)
{
    struct priv *p = ctx->hwdec_priv;

    talloc_free(p);

    av_freep(&ctx->avctx->hwaccel_context);
}

static int init(struct lavc_ctx *ctx)
{
    struct priv *p = talloc_ptrtype(NULL, p);
    *p = (struct priv) {
        .log = mp_log_new(p, ctx->log, "aml"),
        .mpaml = hwdec_devices_get(ctx->hwdec_devs, HWDEC_AML)->ctx,
    };
    ctx->hwdec_priv = p;

    return 0;
}

static int probe(struct lavc_ctx *ctx, struct vd_lavc_hwdec *hwdec,
                 const char *codec)
{
    if (!hwdec_devices_load(ctx->hwdec_devs, HWDEC_AML))
        return HWDEC_ERR_NO_CTX;
    return 0;
}

const struct vd_lavc_hwdec mp_vd_lavc_aml = {
    .type = HWDEC_AML,
    .image_format = IMGFMT_AML,
    .probe = probe,
    .init = init,
    .uninit = uninit,
    .init_decoder = init_decoder,
};
