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

#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>

#include <pthread.h>

#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_mf.h>
#include <libavutil/opt.h>

#include "lavc.h"
#include "common/common.h"
#include "video/fmt-conversion.h"
#include "video/hwdec.h"
#include "video/mp_image_pool.h"

struct priv {
    struct mp_log *log;
    AVBufferRef *device_ref; // AVHWDeviceContext
    AVMFDeviceContext *mf_hwctx;
    struct mp_image_pool *sw_pool;
};

static pthread_once_t mf_init_once = PTHREAD_ONCE_INIT;
// MinGW import libs are missing this, apparently.
static HRESULT (WINAPI *mp_MFGetService)(
    IUnknown *punkObject,
    REFGUID  guidService,
    REFIID   riid,
    LPVOID   *ppvObject
    );

static void mf_init(void)
{
    HANDLE lib = LoadLibrary(L"mf.dll");
    if (lib)
        mp_MFGetService = (void *)GetProcAddress(lib, "MFGetService");
}

static struct mp_image *copy_image(struct lavc_ctx *ctx, struct mp_image *img)
{
    struct priv *p = ctx->hwdec_priv;
    if (img->imgfmt != IMGFMT_MF || !img->hwctx)
        return img;
    AVHWFramesContext *fctx = (void *)img->hwctx->data;
    struct mp_image *new = mp_image_pool_get(p->sw_pool,
                                pixfmt2imgfmt(fctx->sw_format), img->w, img->h);
    if (!new)
        return img;
    // av_hwframe_transfer_data() requires fully refcounted frames, so a
    // mad conversion dance is needed.
    AVFrame *new_av = mp_image_to_av_frame(new);
    AVFrame *img_av = mp_image_to_av_frame(img);
    TA_FREEP(&new);
    if (new_av && img_av && av_hwframe_transfer_data(new_av, img_av, 0) >= 0)
        new = mp_image_from_av_frame(new_av);
    av_frame_free(&new_av);
    av_frame_free(&img_av);
    if (new)
        mp_image_copy_attributes(new, img);
    talloc_free(img);
    return new;
}

struct wrapped_ref {
    struct mp_image *orig;
    IUnknown *ref;
};

static void wrapped_ref_free(void *arg)
{
    struct wrapped_ref *ref = arg;
    IUnknown_Release(ref->ref);
    talloc_free(ref->orig);
    talloc_free(ref);
}

static struct mp_image *wrap_d3d_img(struct lavc_ctx *ctx, struct mp_image *img)
{
    struct priv *p = ctx->hwdec_priv;
    HRESULT hr;

    if (img->imgfmt != IMGFMT_MF)
        return img;

    AVHWFramesContext *fctx = (void *)img->hwctx->data;
    IMFSample *sample = (void *)img->planes[3];
    DWORD num_buffers;
    IMFMediaBuffer *buffer = NULL;

    hr = IMFSample_GetBufferCount(sample, &num_buffers);
    if (FAILED(hr) || num_buffers != 1)
        goto error;

    hr = IMFSample_GetBufferByIndex(sample, 0, &buffer);
    if (FAILED(hr))
        goto error;

    if (p->mf_hwctx->d3d11_manager) {
        ID3D11Texture2D *tex = NULL;
        UINT subindex = 0;
        IMFDXGIBuffer *dxgi_buffer = NULL;
        hr = IMFMediaBuffer_QueryInterface(buffer, &IID_IMFDXGIBuffer,
                                           (void **)&dxgi_buffer);
        if (!FAILED(hr)) {
            IMFDXGIBuffer_GetResource(dxgi_buffer, &IID_ID3D11Texture2D,
                                      (void **)&tex);
            IMFDXGIBuffer_GetSubresourceIndex(dxgi_buffer, &subindex);
            IMFDXGIBuffer_Release(dxgi_buffer);
        }
        if (!tex) {
            MP_ERR(ctx, "no texture\n");
            goto error;
        }
        struct wrapped_ref *ref = talloc_zero(NULL, struct wrapped_ref);
        ref->orig = img;
        ref->ref = (IUnknown *)tex;
        struct mp_image *new = mp_image_new_custom_ref(NULL, ref, wrapped_ref_free);
        if (!new)
            abort();
        mp_image_setfmt(new, IMGFMT_D3D11VA);
        mp_image_set_size(new, img->w, img->h);
        mp_image_copy_attributes(new, img);
        new->params.hw_subfmt = fctx ? pixfmt2imgfmt(fctx->sw_format) : 0;
        if (new->params.hw_subfmt == IMGFMT_NV12)
            mp_image_setfmt(new, IMGFMT_D3D11NV12);
        new->planes[1] = (void *)tex;
        new->planes[2] = (void *)(intptr_t)subindex;
        img = new;
    } else if (p->mf_hwctx->d3d9_manager) {
        IDirect3DSurface9 *surface = NULL;
        if (!mp_MFGetService)
            goto error;
        hr = mp_MFGetService((IUnknown *)buffer, &MR_BUFFER_SERVICE,
                             &IID_IDirect3DSurface9, (void **)&surface);
        if (FAILED(hr)) {
            MP_ERR(ctx, "no buffer\n");
            goto error;
        }
        struct wrapped_ref *ref = talloc_zero(NULL, struct wrapped_ref);
        ref->orig = img;
        ref->ref = (IUnknown *)surface;
        struct mp_image *new = mp_image_new_custom_ref(NULL, ref, wrapped_ref_free);
        if (!new)
            abort();
        mp_image_setfmt(new, IMGFMT_DXVA2);
        mp_image_set_size(new, img->w, img->h);
        mp_image_copy_attributes(new, img);
        new->planes[3] = (void *)surface;
        img = new;
    } else {
        abort();
    }

    if (buffer)
        IMFMediaBuffer_Release(buffer);
    return img;

error:
    MP_ERR(ctx, "error reading surface\n");
    ctx->hwdec_failed = true;
    if (buffer)
        IMFMediaBuffer_Release(buffer);
    return img;
}

static void uninit(struct lavc_ctx *ctx)
{
    struct priv *p = ctx->hwdec_priv;

    ctx->avctx->hwaccel_context = NULL;

    av_buffer_unref(&p->device_ref);
    talloc_free(p);
}

static int init(struct lavc_ctx *ctx)
{
    pthread_once(&mf_init_once, mf_init);

    struct priv *p = talloc_ptrtype(NULL, p);
    *p = (struct priv) {
        .log = mp_log_new(p, ctx->log, "mf"),
    };
    ctx->hwdec_priv = p;

    p->device_ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_MF);
    if (!p->device_ref)
        return -1;

    AVHWDeviceContext *hwctx = (void *)p->device_ref->data;
    p->mf_hwctx = hwctx->hwctx;

    if (ctx->hwdec->type == HWDEC_MF) {
        ID3D11Device *d3d11 = hwdec_devices_load(ctx->hwdec_devs, HWDEC_D3D11VA);
        IDirect3DDevice9 *d3d9 = hwdec_devices_load(ctx->hwdec_devs, HWDEC_DXVA2);
        if (d3d11) {
            ID3D11Device_AddRef(d3d11);
            p->mf_hwctx->init_d3d11_device = d3d11;
            p->mf_hwctx->device_type = AV_MF_D3D11;

            // For now, we always use a video processor. If we want to bind
            // it as texture later, D3D11_BIND_SHADER_RESOURCE is needed.
            int bind_flags = D3D11_BIND_DECODER;
            av_opt_set_int(ctx->avctx, "d3d_bind_flags", bind_flags,
                           AV_OPT_SEARCH_CHILDREN);
        } else if (d3d9) {
            IDirect3DDevice9_AddRef(d3d9);
            p->mf_hwctx->init_d3d9_device = d3d9;
            p->mf_hwctx->device_type = AV_MF_D3D9;
        }
    } else {
        p->sw_pool = talloc_steal(p, mp_image_pool_new(10));
        p->mf_hwctx->device_type = AV_MF_AUTO;
    }

    if (av_hwdevice_ctx_init(p->device_ref) < 0)
        return -1;

    if (p->mf_hwctx->d3d11_manager) {
        MP_VERBOSE(ctx, "Using D3D11.\n");
    } else if (p->mf_hwctx->d3d9_manager) {
        MP_VERBOSE(ctx, "Using D3D9.\n");
    } else {
        MP_ERR(ctx, "Not actually using hardware decoding.\n");
        return -1;
    }

    ctx->avctx->hwaccel_context = p->device_ref;

    av_opt_set_int(ctx->avctx, "require_d3d", 1, AV_OPT_SEARCH_CHILDREN);

    return 0;
}

static int probe(struct lavc_ctx *ctx, struct vd_lavc_hwdec *hwdec,
                 const char *codec)
{
    if (hwdec->type != HWDEC_MF_COPY) {
        // Any of those work.
        if (!hwdec_devices_load(ctx->hwdec_devs, HWDEC_D3D11VA) &&
            !hwdec_devices_load(ctx->hwdec_devs, HWDEC_DXVA2))
            return -1;
    }
    return 0;
}

const struct vd_lavc_hwdec mp_vd_lavc_mf = {
    .type = HWDEC_MF,
    .image_format = IMGFMT_MF,
    .lavc_suffix = "_mf",
    .probe = probe,
    .init = init,
    .uninit = uninit,
    .process_image = wrap_d3d_img,
};

const struct vd_lavc_hwdec mp_vd_lavc_mf_copy = {
    .type = HWDEC_MF_COPY,
    .copying = true,
    .image_format = IMGFMT_MF,
    .lavc_suffix = "_mf",
    .probe = probe,
    .init = init,
    .uninit = uninit,
    .process_image = copy_image,
};
