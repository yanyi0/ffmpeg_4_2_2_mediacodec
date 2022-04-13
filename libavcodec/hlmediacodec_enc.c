#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/pixfmt.h"
#include "internal.h"
#include "encode.h"
#include "hlmediacodec.h"
#include "hlmediacodec_codec.h"
#include "mediacodec_wrapper.h"

static av_cold int hlmediacodec_encode_init(AVCodecContext *avctx)
{
    AMediaFormat *format = NULL;
    HLMediaCodecEncContext *ctx = avctx->priv_data;
    int ret = 0;

    do
    {
        hi_logi(avctx, "%s %d init start globalHdr: [%d %s] rc_mode: %d", __FUNCTION__, __LINE__,
                avctx->flags, (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? "yes" : "no", ctx->rc_mode);

        ctx->stats.init_stamp = av_gettime_relative();
        ctx->frame = av_frame_alloc();
        if (!ctx->frame)
        {
            ret = AVERROR(ENOMEM);
            break;
        }

        if (!(ctx->mediaformat = AMediaFormat_new()))
        {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "AMediaFormat_new fail");
            break;
        }

        if ((ret = hlmediacodec_fill_format(avctx, ctx->mediaformat)) != 0)
        {
            hi_loge(avctx, "%s %d mediacodec_encode_fill_format failed (%d)!", __FUNCTION__, __LINE__, ret);
            break;
        }

        if ((ret = hlmediacodec_encode_header(avctx)) != 0)
        {
            hi_loge(avctx, "%s %d mediacodec_encode_header failed (%d)!", __FUNCTION__, __LINE__, ret);
            break;
        }

        const char *mime = ff_hlmediacodec_get_mime(avctx->codec_id);
        if (!mime)
        {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "%s %d codec (%d) unsupport!", __FUNCTION__, __LINE__, avctx->codec_id);
            break;
        }

        if (!(ctx->mediacodec = AMediaCodec_createEncoderByType(mime)))
        {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "%s %d AMediaCodec_createEncoderByType failed!", __FUNCTION__, __LINE__);
            break;
        }

        hi_logi(avctx, "%s %d AMediaCodec_configure %s format %s", __FUNCTION__, __LINE__, mime, AMediaFormat_toString(ctx->mediaformat));

        media_status_t status = AMEDIA_OK;
        if ((status = AMediaCodec_configure(ctx->mediacodec, ctx->mediaformat, NULL, 0, HLMEDIACODEC_CONFIGURE_FLAG_ENCODE)))
        {
            ret = AVERROR(EINVAL);
            hi_loge(avctx, "%s %d AMediaCodec_configure failed (%d) !", __FUNCTION__, __LINE__, status);
            break;
        }

        if ((status = AMediaCodec_start(ctx->mediacodec)))
        {
            ret = AVERROR(EIO);
            hi_loge(avctx, "%s %d AMediaCodec_start failed (%d) !", __FUNCTION__, __LINE__, status);
            break;
        }

        ctx->inited = true;
    } while (0);

    hi_logi(avctx, "%s %d init ret (%d)", __FUNCTION__, __LINE__, ret);
    return ret;
}

static int hlmediacodec_enc_send(AVCodecContext *avctx)
{
    HLMediaCodecEncContext *ctx = avctx->priv_data;

    int ret = 0;

    do
    {
        int get_ret = ff_encode_get_frame(avctx, ctx->frame);
        hi_logd(avctx, "ffmpeg encode.c %s %d %d %d", __FUNCTION__, __LINE__, get_ret, ctx->frame->pkt_size);
        if (get_ret != 0)
        {
            ctx->stats.get_fail_cnt++;

            if (get_ret == AVERROR_EOF)
            {
                ctx->in_eof = true; // flush
                hi_logd(avctx, "ffmpeg %s %d ff_encode_get_frame eof", __FUNCTION__, __LINE__);
            }
            else
            {
                ret = AVERROR(EAGAIN);
                hi_logd(avctx, "ffmpeg %s %d ff_encode_get_frame fail (%d)", __FUNCTION__, __LINE__, get_ret);
                break;
            }
        }
        else
        {
            ctx->stats.get_succ_cnt++;
        }

        int in_times = ctx->in_timeout_times;
        while (true)
        {
            ssize_t in_bufidx = AMediaCodec_dequeueInputBuffer(ctx->mediacodec, ctx->in_timeout);
            if (in_bufidx < 0)
            {
                hi_logd(avctx, "%s %d AMediaCodec_dequeueInputBuffer codec: %p fail (%d) getret: %d times: %d",
                        __FUNCTION__, __LINE__, ctx->mediacodec, in_bufidx, get_ret, in_times);
                ctx->stats.in_fail_cnt++;

                if (in_times-- <= 0)
                {
                    hi_logd(avctx, "%s %d AMediaCodec_dequeueInputBuffer timeout", __FUNCTION__, __LINE__);
                    ret = AVERROR_EXTERNAL;
                    break;
                }

                continue;
            }

            size_t in_buffersize = 0;
            uint8_t *in_buffer = AMediaCodec_getInputBuffer(ctx->mediacodec, in_bufidx, &in_buffersize);
            if (!in_buffer)
            {
                hi_loge(avctx, "%s %d AMediaCodec_getInputBuffer codec: %p fail", __FUNCTION__, __LINE__, ctx->mediacodec);
                ctx->stats.in_fail_cnt++;
                ret = AVERROR_EXTERNAL;
                break;
            }

            if (!ctx->in_eof)
            {
                size_t copy_size = in_buffersize;
                int copy_ret = av_image_copy_to_buffer(in_buffer, in_buffersize, (const uint8_t **)ctx->frame->data,
                                                       ctx->frame->linesize, ctx->frame->format,
                                                       ctx->frame->width, ctx->frame->height, 1);

                if (copy_ret > 0)
                {
                    copy_size = copy_ret;
                }

                int64_t pts = av_rescale_q(ctx->frame->pts, avctx->time_base, AV_TIME_BASE_Q);
                int64_t duration = av_rescale_q(ctx->frame->pkt_duration, avctx->time_base, AV_TIME_BASE_Q);
                in_bufidx = AMediaCodec_queueInputBuffer(ctx->mediacodec, in_bufidx, 0, copy_size, pts, 0);
                ctx->in_pts = pts;
                ctx->in_duration = duration;
            }
            else
            {
                in_bufidx = AMediaCodec_queueInputBuffer(ctx->mediacodec, in_bufidx, 0, 0, 0, HLMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                ctx->in_pts += ctx->in_duration;
                hi_logi(avctx, "%s %d AMediaCodec_queueInputBuffer eof flush", __FUNCTION__, __LINE__);
                //TODO flush finish
                // ret = -1;
            }

            if (in_bufidx != 0)
            {
                ret = AVERROR_EXTERNAL;
                ctx->stats.in_fail_cnt++;
                hi_loge(avctx, "AMediaCodec_queueInputBuffer fail (%d)", in_bufidx);
            }
            else
            {
                ctx->stats.in_succ_cnt++;
            }

            break;
        }
    } while (false);

    av_frame_unref(ctx->frame);
    hi_logi(avctx, "%s %d h264_hlmediacodec (%d) %d", __FUNCTION__, __LINE__,ret,ctx->frame->pkt_size);
    return ret;
}

static int hlmediacodec_enc_recv(AVCodecContext *avctx, AVPacket *pkt)
{
    hi_logi(avctx, "%s %d hlmediacodec_enc_recv encoded copy data (%d)", __FUNCTION__, __LINE__,pkt->size);
    HLMediaCodecEncContext *ctx = avctx->priv_data;

    int ret = 0;
    int ou_times = ctx->ou_timeout_times;
    hi_logi(avctx, "%s %d hlmediacodec_enc_recv encoded copy data ou_times (%d) %d %d", __FUNCTION__, __LINE__,pkt->size,ou_times,avctx->internal->buffer_frame->pkt_size);
    int ou_timeout = ctx->in_eof ? ctx->eof_timeout : ctx->ou_timeout;

    while (true)
    {
        --ou_times;

        FFAMediaCodecBufferInfo bufferInfo = {0};
        
        ssize_t ou_bufidx = AMediaCodec_dequeueOutputBuffer(ctx->mediacodec, &bufferInfo, ctx->ou_timeout);
        hi_logt(avctx, "%s %d AMediaCodec_dequeueOutputBuffer ret (%d) times: %d offset: %d size: %d pts: %llu flags: %u", __FUNCTION__, __LINE__,
                ou_bufidx, ctx->ou_timeout_times - ou_times, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, bufferInfo.flags);
        //TODO signal eof
        // if (ctx->in_eof)
        // {
        //     bufferInfo.flags = 4;
        // }
        hi_logt(avctx, "%s %d AMediaCodec_dequeueOutputBuffer ret (%d) times: %d offset: %d size: %d pts: %llu flags: %u", __FUNCTION__, __LINE__,
                ou_bufidx, ctx->ou_timeout_times - ou_times, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, bufferInfo.flags);
        if (ou_bufidx >= 0)
        {
            ctx->stats.ou_succ_cnt++;

            size_t ou_bufsize = 0;
            uint8_t *ou_buf = AMediaCodec_getOutputBuffer(ctx->mediacodec, ou_bufidx, &ou_bufsize);
            if (!ou_buf)
            {
                hi_loge(avctx, "%s %d AMediaCodec_getOutputBuffer codec: %p fail", __FUNCTION__, __LINE__, ctx->mediacodec);
                AMediaCodec_releaseOutputBuffer(ctx->mediacodec, ou_bufidx, false);
                ret = AVERROR_EXTERNAL;
                break;
            }

            int64_t pts = av_rescale_q(bufferInfo.presentationTimeUs, AV_TIME_BASE_Q, avctx->time_base);

            hi_logt(avctx, "%s %d AMediaCodec OutputBuffer status: %d outsize: %u flags: %d offset: %d size: %d pts: [%lld %lld] nalu: [%x %x %x %x %x %x]",
                    __FUNCTION__, __LINE__, ou_bufidx, ou_bufsize, bufferInfo.flags, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, pts, ou_buf[0], ou_buf[1], ou_buf[2], ou_buf[3], ou_buf[4], ou_buf[5]);

            if ((ret = ff_alloc_packet2(avctx, pkt, bufferInfo.size, bufferInfo.size) < 0))
            {
                hi_loge(avctx, "ff_alloc_packet2 fail(%d)", ret);
                AMediaCodec_releaseOutputBuffer(ctx->mediacodec, ou_bufidx, false);
                ret = AVERROR_EXTERNAL;
                break;
            }

            memcpy(pkt->data, ou_buf, bufferInfo.size);

            pkt->dts = pkt->pts = pts;

            ctx->stats.ou_succ_frame_cnt++;

            if (bufferInfo.flags & HLMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
            {
                ctx->stats.ou_succ_conf_cnt++;
                pkt->flags |= AV_PKT_FLAG_KEY;
            }

            if (bufferInfo.flags & HLMEDIACODEC_CONFIGURE_FLAG_ENCODE)
            {
                ctx->stats.ou_succ_idr_cnt++;
                pkt->flags |= AV_PKT_FLAG_KEY;
            }

            if (bufferInfo.flags & HLMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
            {
                pkt->dts = pkt->pts = ctx->in_pts;
                ctx->ou_eof = true;
                ctx->stats.ou_succ_end_cnt++;
                hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer HLMEDIACODEC_BUFFER_FLAG_END_OF_STREAM", __FUNCTION__, __LINE__);
            }
            hi_logi(avctx, "%s %d hlmediacodec_enc_recv avctx->internal->buffer_frame->pkt_size %d", __FUNCTION__, __LINE__,avctx->internal->buffer_frame->pkt_size);
            AMediaCodec_releaseOutputBuffer(ctx->mediacodec, ou_bufidx, false);
            hi_logi(avctx, "%s %d hlmediacodec_enc_recv avctx->internal->buffer_frame->pkt_size %d", __FUNCTION__, __LINE__,avctx->internal->buffer_frame->pkt_size);
            break;
        }

        ctx->stats.ou_fail_cnt++;
        if (ou_bufidx == AMEDIACODEC_INFO_TRY_AGAIN_LATER)
        {
            hi_logd(avctx, "%s %d AMediaCodec_dequeueOutputBuffer AMEDIACODEC_INFO_TRY_AGAIN_LATER ", __FUNCTION__, __LINE__);

            ctx->stats.ou_fail_again_cnt++;

            if (ou_times <= 0)
            {
                ret = AVERROR(EAGAIN);
                hi_loge(avctx, "%s %d AMediaCodec_dequeueOutputBuffer timeout ", __FUNCTION__, __LINE__);
                break;
            }

            continue;
        }
        else if (ou_bufidx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
        {
            ctx->stats.ou_fail_format_cnt++;

            AMediaFormat *format = AMediaCodec_getOutputFormat(ctx->mediacodec);
            if (format)
            {
                hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED %s",
                        __FUNCTION__, __LINE__, AMediaFormat_toString(format));

                AMediaFormat_delete(format);
            }

            continue;
        }
        else if (ou_bufidx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
        {
            ctx->stats.ou_fail_buffer_cnt++;
            hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED",
                    __FUNCTION__, __LINE__);
            continue;
        }
        else
        {
            hi_loge(avctx, "%s %d AMediaCodec_dequeueOutputBuffer fail (%d)", __FUNCTION__, __LINE__, ou_bufidx);
            ctx->stats.ou_fail_oth_cnt++;
            ret = AVERROR(EAGAIN);
            break;
        }
    }
    hi_loge(avctx, "%s %d received packet (%d) avctx->internal->buffer_frame->pkt_size %d", __FUNCTION__, __LINE__, ret,avctx->internal->buffer_frame->pkt_size);
    return ret;
}
// static av_cold int vtenc_frame(
//     AVCodecContext *avctx,
//     AVPacket       *pkt,
//     const AVFrame  *frame,
//     int            *got_packet){

//     }
static int hlmediacodec_encode_receive_packet(
    AVCodecContext *avctx,
    AVPacket *pkt,
    const AVFrame *frame,
    int *got_packet)
{
    av_log(NULL, AV_LOG_VERBOSE, "ffmpeg hlmediacodec_enc.c %s %d %p %d frame pkt_size '%d' %d %d avctx->internal->buffer_frame->pkt_size %d.\n",__FUNCTION__, __LINE__,frame,(frame==NULL),frame->pkt_size,pkt->size,*got_packet,avctx->internal->buffer_frame->pkt_size);
    HLMediaCodecEncContext *ctx = avctx->priv_data;
    
    // copy frame
    AVCodecInternal *avci = avctx->internal;
    avci->buffer_frame = frame;
    // avctx->internal = avci;
    // av_frame_copy(avctx->internal->buffer_frame, frame);

    av_log(NULL, AV_LOG_VERBOSE, "ffmpeg hlmediacodec_enc.c %s %d  per avci->buffer_frame packet '%d' %d %d frame pkt_size %d.\n",__FUNCTION__, __LINE__,avctx->internal->buffer_frame->pkt_size,pkt->size,*got_packet,frame->pkt_size);
    av_log(NULL, AV_LOG_VERBOSE, "ffmpeg hlmediacodec_enc.c %s %d inited %d in_eof %d ou_eof %d avci->draining %d.\n",__FUNCTION__, __LINE__,ctx->inited,ctx->in_eof,ctx->ou_eof,avctx->internal->draining);
    if (!ctx->inited)
    {
        return AVERROR_EXTERNAL;
    }
    
    if (ctx->in_eof && ctx->ou_eof)
    {
        hi_logi(avctx, "%s %d ", __FUNCTION__, __LINE__);
        return AVERROR_EOF;
    }

    int ret = 0;
    if (!ctx->in_eof)
    {
        if ((ret = hlmediacodec_enc_send(avctx)) != 0)
        {
            return ret;
        }
    }

    int val = hlmediacodec_enc_recv(avctx, pkt);
    if (val == 0)//pkt encoded ,signal got_packet
    {
        *got_packet = 1;
        return val;
    }
    return val;
    
}

static av_cold int hlmediacodec_encode_close(AVCodecContext *avctx)
{
    hi_logi(avctx, "hlmediacodec_encode_close %s %d", __FUNCTION__, __LINE__);

    HLMediaCodecEncContext *ctx = avctx->priv_data;
    ctx->stats.uint_stamp = av_gettime_relative();

    hlmediacodec_show_stats(avctx, ctx->stats);

    if (ctx->mediacodec)
    {
        AMediaCodec_stop(ctx->mediacodec);
        AMediaCodec_delete(ctx->mediacodec);
        ctx->mediacodec = NULL;
    }

    if (ctx->mediaformat)
    {
        AMediaFormat_delete(ctx->mediaformat);
        ctx->mediaformat = NULL;
    }

    if (ctx->frame)
    {
        av_frame_free(&ctx->frame);
        ctx->frame = NULL;
    }

    return 0;
}

#define OFFSET(x) offsetof(HLMediaCodecEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption ff_hlmediacodec_enc_options[] = {
    {"rc-mode", "The bitrate mode to use", OFFSET(rc_mode), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_BITRATE_MODE_VBR}, HLMEDIACODEC_BITRATE_MODE_CQ, HLMEDIACODEC_BITRATE_MODE_CBR, VE, "rc_mode"},
    {"cq", "Constant quality", 0, AV_OPT_TYPE_CONST, {.i64 = HLMEDIACODEC_BITRATE_MODE_CQ}, INT_MIN, INT_MAX, VE, "rc_mode"},
    {"vbr", "Variable bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = HLMEDIACODEC_BITRATE_MODE_VBR}, INT_MIN, INT_MAX, VE, "rc_mode"},
    {"cbr", "Constant bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = HLMEDIACODEC_BITRATE_MODE_CBR}, INT_MIN, INT_MAX, VE, "rc_mode"},
    {"in_timeout", "in buff timeout", OFFSET(in_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_IN_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VE},
    {"ou_timeout", "ou buff timeout", OFFSET(ou_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_OU_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VE},
    {"eof_timeout", "eof buff timeout", OFFSET(eof_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_EOF_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VE},
    {"in_timeout_times", "in buff timeout times", OFFSET(in_timeout_times), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_IN_SET_TIMEOUT_TIMES}, HLMEDIACODEC_MIN_TIMEOUT_TIMES, HLMEDIACODEC_MAX_TIMEOUT_TIMES, VE},
    {"ou_timeout_times", "ou buff timeout times", OFFSET(ou_timeout_times), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_ENC_OU_SET_TIMEOUT_TIMES}, HLMEDIACODEC_MIN_TIMEOUT_TIMES, HLMEDIACODEC_MAX_TIMEOUT_TIMES, VE},
    {NULL},
};

#define DECLARE_HLMEDIACODEC_VCLASS(short_name)                       \
    static const AVClass ff_##short_name##_hlmediacodec_enc_class = { \
        .class_name = #short_name "_hlmediacodec",                    \
        .item_name = av_default_item_name,                            \
        .option = ff_hlmediacodec_enc_options,                        \
        .version = LIBAVUTIL_VERSION_INT,                             \
    };

// receive_packet modify to encode2
#define DECLARE_HLMEDIACODEC_ENC(short_name, full_name, codec_id, codec_type)                           \
    DECLARE_HLMEDIACODEC_VCLASS(short_name)                                                             \
    AVCodec ff_##short_name##_hlmediacodec_encoder = {                                                  \
        .name = #short_name "_hlmediacodec",                                                            \
        .long_name = full_name " (Ffmpeg MediaCodec NDK)",                                              \
        .type = codec_type,                                                                             \
        .id = codec_id,                                                                                 \
        .priv_class = &ff_##short_name##_hlmediacodec_enc_class,                                        \
        .priv_data_size = sizeof(HLMediaCodecEncContext),                                               \
        .init = hlmediacodec_encode_init,                                                               \
        .encode2 = hlmediacodec_encode_receive_packet,                                                  \
        .close = hlmediacodec_encode_close,                                                             \
        .capabilities = AV_CODEC_CAP_DELAY,                                                             \
        .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,                      \
        .pix_fmts = (const enum AVPixelFormat[]){AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE}, \
    };

#ifdef CONFIG_H264_HLMEDIACODEC_ENCODER
DECLARE_HLMEDIACODEC_ENC(h264, "H.264", AV_CODEC_ID_H264, AVMEDIA_TYPE_VIDEO)
#endif
