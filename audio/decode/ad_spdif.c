/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>

#include "config.h"
#include "core/mp_msg.h"

#include "core/codec-cfg.h"
#include "audio/format.h"

#include "stream/stream.h"
#include "demux/demux.h"
#include "demux/stheader.h"

#include "ad_internal.h"

#include "core/options.h"

static const ad_info_t info = {
    "libavformat/spdifenc audio pass-through decoder.",
    "spdif",
    "Naoya OYAMA",
    "Naoya OYAMA",
    "For ALL hardware decoders"
};

LIBAD_EXTERN(spdif)

#define FILENAME_SPDIFENC "spdif"
#define OUTBUF_SIZE 65536
struct spdifContext {
    AVCodecContext  *lavc_ctx;
    AVFormatContext *lavf_ctx;
    int              init_buffer_len;
    int              init_buffer_pos;
    int              initialized;
    int              iec61937_packet_size;
    int              out_buffer_len;
    int              out_buffer_size;
    uint8_t         *out_buffer;
    uint8_t          init_buffer[OUTBUF_SIZE];
    uint8_t          pb_buffer[OUTBUF_SIZE];
};

static int read_packet(void *p, uint8_t *buf, int buf_size)
{
    // spdifenc does not use read callback.
    return 0;
}

static int write_packet(void *p, uint8_t *buf, int buf_size)
{
    int len;
    struct spdifContext *ctx = p;

    len = FFMIN(buf_size, ctx->out_buffer_size -ctx->out_buffer_len);
    memcpy(&ctx->out_buffer[ctx->out_buffer_len], buf, len);
    ctx->out_buffer_len += len;
    return len;
}

static int64_t seek(void *p, int64_t offset, int whence)
{
    // spdifenc does not use seek callback.
    return 0;
}

static int preinit(sh_audio_t *sh)
{
    sh->samplesize = 2;
    return 1;
}

static int init(sh_audio_t *sh)
{
    int i, x, srate, bps, counter_max, *dtshd_rate;
    char tmpstr[50];
    static const struct {
        const char *name; enum CodecID id;
    } fmt_id_type[] = {
        { "aac"   , CODEC_ID_AAC    },
        { "ac3"   , CODEC_ID_AC3    },
        { "dca"   , CODEC_ID_DTS    },
        { "eac3"  , CODEC_ID_EAC3   },
        { "mp3"   , CODEC_ID_MP3    },
        { "truehd", CODEC_ID_TRUEHD },
        { NULL    , 0 }
    };
    AVCodecContext      *lavc_ctx   = NULL;
    AVCodec             *lavc_codec = NULL;
    AVDictionary        *opts       = NULL;
    AVFormatContext     *lavf_ctx   = NULL;
    AVStream            *stream     = NULL;
    const AVOption      *opt        = NULL;
    struct spdifContext *spdif_ctx  = NULL;

    sh->needs_parsing = 1;

    spdif_ctx = av_mallocz(sizeof(*spdif_ctx));
    if (!spdif_ctx)
        goto fail;
    spdif_ctx->lavf_ctx = avformat_alloc_context();
    if (!spdif_ctx->lavf_ctx)
        goto fail;
    spdif_ctx->initialized     = 0;
    spdif_ctx->init_buffer_len = 0;
    spdif_ctx->init_buffer_pos = 0;

    sh->context = spdif_ctx;
    lavf_ctx    = spdif_ctx->lavf_ctx;

    lavf_ctx->oformat = av_guess_format(FILENAME_SPDIFENC, NULL, NULL);
    if (!lavf_ctx->oformat)
        goto fail;
    lavf_ctx->priv_data = av_mallocz(lavf_ctx->oformat->priv_data_size);
    if (!lavf_ctx->priv_data)
        goto fail;
    lavf_ctx->pb = avio_alloc_context(spdif_ctx->pb_buffer, OUTBUF_SIZE, 1, spdif_ctx,
                                      read_packet, write_packet, seek);
    if (!lavf_ctx->pb)
        goto fail;
    stream = avformat_new_stream(lavf_ctx, 0);
    if (!stream)
        goto fail;
    lavf_ctx->duration   = AV_NOPTS_VALUE;
    lavf_ctx->start_time = AV_NOPTS_VALUE;
    for (i = 0; fmt_id_type[i].name; i++) {
        if (!strcmp(sh->codec->dll, fmt_id_type[i].name)) {
            lavf_ctx->streams[0]->codec->codec_id = fmt_id_type[i].id;
            break;
        }
    }
    lavf_ctx->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;
    if (AVERROR_PATCHWELCOME == lavf_ctx->oformat->write_header(lavf_ctx)) {
        mp_msg(MSGT_DECAUDIO,MSGL_INFO,
               "This codec is not supported by spdifenc.\n");
        goto fail;
    }

    lavc_codec = avcodec_find_decoder_by_name(sh->codec->dll);
    if (!lavc_codec){
        mp_msg(MSGT_DECAUDIO,MSGL_ERR,"Cannot find codec '%s' in libavcodec...\n",sh->codec->dll);
        goto fail;
    }
    // create avcodec
    lavc_ctx              = avcodec_alloc_context3(lavc_codec);
    spdif_ctx->lavc_ctx   = lavc_ctx;
    snprintf(tmpstr, sizeof(tmpstr), "%f", sh->opts->drc_level);
    av_dict_set(&opts, "drc_scale", tmpstr, 0);
    lavc_ctx->sample_rate = sh->samplerate;
    lavc_ctx->bit_rate    = sh->i_bps * 8;
    if (sh->wf) {
        lavc_ctx->channels              = sh->wf->nChannels;
        lavc_ctx->sample_rate           = sh->wf->nSamplesPerSec;
        lavc_ctx->bit_rate              = sh->wf->nAvgBytesPerSec * 8;
        lavc_ctx->block_align           = sh->wf->nBlockAlign;
        lavc_ctx->bits_per_coded_sample = sh->wf->wBitsPerSample;
    }
    lavc_ctx->request_channels = sh->opts->audio_output_channels;
    lavc_ctx->codec_tag        = sh->format; //FOURCC
    lavc_ctx->codec_id         = lavc_codec->id; // not sure if required, imho not --A'rpi

    // alloc extra data
    if (sh->wf && sh->wf->cbSize > 0) {
        lavc_ctx->extradata = av_mallocz(sh->wf->cbSize
                              + FF_INPUT_BUFFER_PADDING_SIZE);
        lavc_ctx->extradata_size = sh->wf->cbSize;
        memcpy(lavc_ctx->extradata, sh->wf + 1,
               lavc_ctx->extradata_size);
    }

    // for QDM2
    if (sh->codecdata_len && sh->codecdata
        && !lavc_ctx->extradata) {
        lavc_ctx->extradata = av_malloc(sh->codecdata_len);
        lavc_ctx->extradata_size = sh->codecdata_len;
        memcpy(lavc_ctx->extradata, (char *)sh->codecdata,
               lavc_ctx->extradata_size);
    }

    // open it
    if (avcodec_open2(lavc_ctx, lavc_codec, &opts) < 0) {
        mp_msg(MSGT_DECAUDIO,MSGL_ERR, "Could not open codec.\n");
        return 0;
    }
    av_dict_free(&opts);
    mp_msg(MSGT_DECAUDIO,MSGL_V,"INFO: libavcodec \"%s\" init OK!\n",
           lavc_codec->name);

    // Decode at least 1 byte:  (to get header filled & packet_size)
    switch (lavc_ctx->codec_id) {
    case CODEC_ID_EAC3:
        counter_max = 11; // EAC3 decoder require 6 packets.
        break;
    case CODEC_ID_TRUEHD:
        counter_max = 47; // TrueHD decoder require 24 packets.
        break;
    default:
        counter_max = 5;
        break;
    }
    for (x = i = 0; i < counter_max && x <= 0; i++) {
        x = decode_audio(sh, spdif_ctx->init_buffer, 1,
                         sizeof(spdif_ctx->init_buffer));
        spdif_ctx->init_buffer_len = x;
    }
    if (x <= 0)
        goto fail;
    spdif_ctx->iec61937_packet_size = x;
    bps = lavc_ctx->bit_rate;
    if (!bps) {
        if (sh->avctx) {
            if (sh->avctx->bit_rate)
                bps = sh->avctx->bit_rate;
            else
                bps = 768000;
        } else {
            bps = 768000;
        }
    }
    srate = lavc_ctx->sample_rate;
    if (!srate) {
        if (sh->avctx) {
            if (sh->avctx->sample_rate)
                srate = sh->avctx->sample_rate;
            else
                srate = 48000;
        } else {
            srate = 48000;
        }
    }

    // setup sh
    sh->channels      = 2;
    sh->i_bps         = bps/8;
    sh->sample_format = AF_FORMAT_AC3_LE;
    sh->samplerate    = srate;
    sh->samplesize    = 2;
    switch (lavc_ctx->codec_id) {
    case CODEC_ID_AAC:
        break;
    case CODEC_ID_AC3:
        sh->sample_format = AF_FORMAT_AC3_BE;
        break;
    case CODEC_ID_DTS:
        opt = av_opt_find(&lavf_ctx->oformat->priv_class,
                          "dtshd_rate", NULL, 0, 0);
        if (!opt)
            goto fail;
        dtshd_rate = (int*)(((uint8_t*)lavf_ctx->priv_data) + opt->offset);
        switch (lavc_ctx->profile) {
        case FF_PROFILE_DTS_HD_HRA:
            *dtshd_rate                     = 192000;
            spdif_ctx->iec61937_packet_size = 8192;
            sh->samplerate                  = 192000;
            // init_buffer_len is result of the DTS core decoded value.
            spdif_ctx->init_buffer_len      = 0;
            break;
        case FF_PROFILE_DTS_HD_MA:
            *dtshd_rate                     = 768000;
            spdif_ctx->iec61937_packet_size = 32768;
            sh->samplerate                  = 192000;
            sh->channels                    = 8;
            // init_buffer_len is result of the DTS core decoded value.
            spdif_ctx->init_buffer_len      = 0;
            break;
        case FF_PROFILE_DTS:
        case FF_PROFILE_DTS_ES:
        case FF_PROFILE_DTS_96_24:
        default:
            *dtshd_rate                     = 0;
            break;
        }
        break;
    case CODEC_ID_EAC3:
        sh->samplerate = 192000;
        break;
    case CODEC_ID_MP3:
        sh->sample_format = AF_FORMAT_MPEG2;
        break;
    case CODEC_ID_TRUEHD:
        sh->channels = 8;
        switch (srate) {
        case 44100:
        case 88200:
        case 176400:
            sh->samplerate = 176400;
            break;
        case 48000:
        case 96000:
        case 192000:
        default:
            sh->samplerate = 192000;
            break;
        }
        break;
    default:
        break;
    }

    mp_msg(MSGT_DECAUDIO,MSGL_V,"spdif packet size: %d.\n",
           spdif_ctx->iec61937_packet_size);

    spdif_ctx->initialized = 1;
    return 1;

fail:
    uninit(sh);
    return 0;
}

static int decode_audio(sh_audio_t *sh, unsigned char *buf,
                        int minlen, int maxlen)
{
    struct spdifContext *spdif_ctx = sh->context;
    AVFormatContext     *lavf_ctx  = spdif_ctx->lavf_ctx;
    AVPacket            pkt;
    double              pts;
    int                 ret, in_size, consumed, x, y;
    int                 len = maxlen;
    unsigned char       *start = NULL;

    consumed = spdif_ctx->out_buffer_len  = 0;
    spdif_ctx->out_buffer_size = maxlen;
    spdif_ctx->out_buffer      = buf;
    while (spdif_ctx->out_buffer_len + spdif_ctx->iec61937_packet_size < maxlen
           && spdif_ctx->out_buffer_len < minlen) {
        if (spdif_ctx->init_buffer_len > 0 && spdif_ctx->initialized) {
            x = write_packet(spdif_ctx,
                             &spdif_ctx->init_buffer[spdif_ctx->init_buffer_pos],
                             spdif_ctx->init_buffer_len);
            spdif_ctx->init_buffer_pos += x;
            spdif_ctx->init_buffer_len -= x;
            continue;
        }
        if (sh->ds->eof)
            break;
        x = ds_get_packet_pts(sh->ds, &start, &pts);
        if (x <= 0) {
            x = 0;
            ds_parse(sh->ds, &start, &x, MP_NOPTS_VALUE, 0);
            if (x == 0)
                continue; // END_NOT_FOUND
            in_size = x;
        } else {
            in_size = x;
            consumed = ds_parse(sh->ds, &start, &x, pts, 0);
            if (x == 0) {
                mp_msg(MSGT_DECAUDIO,MSGL_V,
                       "start[%p] in_size[%d] consumed[%d] x[%d].\n",
                       start, in_size, consumed, x);
                continue; // END_NOT_FOUND
            }
            sh->ds->buffer_pos -= in_size - consumed;
        }
        av_init_packet(&pkt);
        pkt.data = start;
        pkt.size = x;
        mp_msg(MSGT_DECAUDIO,MSGL_V,
               "start[%p] pkt.size[%d] in_size[%d] consumed[%d] x[%d].\n",
               start, pkt.size, in_size, consumed, x);
        if (pts != MP_NOPTS_VALUE) {
            sh->pts       = pts;
            sh->pts_bytes = 0;
        }
        if (!spdif_ctx->initialized) {
            y = avcodec_decode_audio3(spdif_ctx->lavc_ctx,
                                      (int16_t*)buf, &len, &pkt);
            if (y == AVERROR(EAGAIN))
                continue;
            if (y < 0)
                abort();
            ret =lavf_ctx->oformat->write_packet(lavf_ctx, &pkt);
            if (ret < 0)
                return ret;
            break;
        }
        ret = lavf_ctx->oformat->write_packet(lavf_ctx, &pkt);
        if (ret < 0)
            break;
    }
    if (spdif_ctx->initialized)
        sh->pts_bytes += spdif_ctx->out_buffer_len;
    return spdif_ctx->out_buffer_len;
}

static int control(sh_audio_t *sh, int cmd, void* arg, ...)
{
    unsigned char *start;
    double pts;

    switch (cmd) {
    case ADCTRL_RESYNC_STREAM:
    case ADCTRL_SKIP_FRAME:
        ds_get_packet_pts(sh->ds, &start, &pts);
        return CONTROL_TRUE;
    }
    return CONTROL_UNKNOWN;
}

static void uninit(sh_audio_t *sh)
{
    struct spdifContext *spdif_ctx = sh->context;
    AVFormatContext     *lavf_ctx  = spdif_ctx->lavf_ctx;
    AVCodecContext      *lavc_ctx  = spdif_ctx->lavc_ctx;

    if (lavc_ctx) {
        avcodec_close(lavc_ctx);
        av_freep(&lavc_ctx->extradata);
        av_freep(&lavc_ctx);
    }
    if (lavf_ctx) {
        if (lavf_ctx->oformat)
            lavf_ctx->oformat->write_trailer(lavf_ctx);
        av_freep(&lavf_ctx->pb);
        if (lavf_ctx->streams) {
            av_freep(&lavf_ctx->streams[0]->codec);
            av_freep(&lavf_ctx->streams[0]->info);
            av_freep(&lavf_ctx->streams[0]);
        }
        av_freep(&lavf_ctx->streams);
        av_freep(&lavf_ctx->priv_data);
    }
    av_freep(&lavf_ctx);
    av_freep(&spdif_ctx);
}
