#include "libavutil/opt.h"
#include "libavutil/buffer_internal.h"
#include "libavutil/avassert.h"
#include "avcodec.h"
#include "decode.h"
#include "internal.h"
#include "h264.h"
#include "hwconfig.h"
#include "bsf.h"
#include "hlmediacodec.h"
#include "hlmediacodec_codec.h"


static av_cold int hlmediacodec_decode_init(AVCodecContext *avctx) {
    hi_logi(avctx, "%s %d codecId: %d thread_cnt: %d width: %d height: %d pix_fmt: %d", 
        __FUNCTION__, __LINE__, avctx->codec_id, avctx->thread_count, avctx->width, avctx->height, avctx->pix_fmt);

    HLMediaCodecDecContext *ctx = avctx->priv_data;
    int ret = 0;

    do {
        ctx->stats.init_stamp = av_gettime_relative();

        int buff_size = hlmediacodec_get_buffer_size(avctx);
        if (buff_size <= 0) {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "hlmediacodec_get_buffer_size fail");
            break;
        }

        if (!(ctx->buffer = av_malloc(buff_size))) {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "av_malloc fail");
            break;
        }

        ctx->buffer_size = buff_size;

        if (!(ctx->mediaformat = AMediaFormat_new())) {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "AMediaFormat_new fail");
            break;
        }

        const char* mime = ff_hlmediacodec_get_mime(avctx->codec_id);
        if (!mime) {
            ret = AVERROR_PROTOCOL_NOT_FOUND;
            hi_loge(avctx, "ff_hlmediacodec_get_mime fail (%d)", avctx->codec_id);
            break;
        }

        if (!(ctx->mediacodec = AMediaCodec_createDecoderByType(mime))) {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "AMediaCodec_createDecoderByType fail (%s)", mime);
            break;
        }

        if ((ret = hlmediacodec_fill_format(avctx, ctx->mediaformat))) {
            break;
        }

        hi_logi(avctx, "AMediaCodec_configure %s format: %s", mime, AMediaFormat_toString(ctx->mediaformat));

        media_status_t status = AMEDIA_OK;
        if ((status = AMediaCodec_configure(ctx->mediacodec, ctx->mediaformat, NULL, NULL, 0))) {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "AMediaCodec_configure fail (%d)", status);
            break;
        }

        if ((status = AMediaCodec_start(ctx->mediacodec))) {
            ret = AVERROR_EXTERNAL;
            hi_loge(avctx, "AMediaCodec_start fail (%d)", status);
            break;
        }

        ctx->inited = true;
    } while (false);

    hi_logi(avctx, "%s %d init codec: %p ret (%d)", __FUNCTION__, __LINE__, ctx->mediacodec, ret);
    return ret;
}

static int hlmediacodec_dec_send(AVCodecContext *avctx) {
    HLMediaCodecDecContext *ctx = avctx->priv_data;

    int ret = 0;
    do {
        int get_ret = ff_decode_get_packet(avctx, &ctx->packet);
        hi_logd(avctx, "%s %d ff_decode_get_packet ret (%d)", __FUNCTION__, __LINE__, get_ret);
        if (get_ret != 0) {
            ctx->stats.get_fail_cnt ++;

            if (get_ret == AVERROR_EOF) {
                ctx->in_eof = true;// flush
                hi_logd(avctx, "%s %d ff_decode_get_packet eof", __FUNCTION__, __LINE__);
            } else {
                ret = AVERROR(EAGAIN);
                hi_logd(avctx, "%s %d ff_decode_get_packet fail (%d)", __FUNCTION__, __LINE__, get_ret);
                break;
            }
        } else {
            ctx->stats.get_succ_cnt ++;
        } 

        int in_times = ctx->in_timeout_times;
        while (true) {
            if (in_times -- <= 0) {
                hi_logd(avctx, "%s %d AMediaCodec_dequeueInputBuffer timeout", __FUNCTION__, __LINE__);
                ret = AVERROR_EXTERNAL;
                break;
            }

            ssize_t in_bufidx = AMediaCodec_dequeueInputBuffer(ctx->mediacodec, ctx->in_timeout);
            hi_logt(avctx, "%s %d AMediaCodec_dequeueInputBuffer ret (%d) times: %d getret: %d", __FUNCTION__, __LINE__, in_bufidx, ctx->in_timeout_times - in_times, get_ret);

            if (in_bufidx < 0) {
                hi_logd(avctx, "%s %d AMediaCodec_dequeueInputBuffer codec: %p fail (%d)", __FUNCTION__, __LINE__, ctx->mediacodec, in_bufidx);
                ctx->stats.in_fail_cnt ++;
                continue;
            }

            size_t in_buffersize = 0;
            uint8_t* in_buffer = AMediaCodec_getInputBuffer(ctx->mediacodec, in_bufidx, &in_buffersize);
            if (!in_buffer) {
                hi_loge(avctx, "%s %d AMediaCodec_getInputBuffer codec: %p fail", __FUNCTION__, __LINE__, ctx->mediacodec);
                ctx->stats.in_fail_cnt ++;
                ret = AVERROR_EXTERNAL;
                break;
            }

            if (!ctx->in_eof) {
                if (ctx->packet.size > in_buffersize) {
                    hi_loge(avctx, "%s %d AMediaCodec_queueInputBuffer codec: %p fail (%u %u)", __FUNCTION__, __LINE__, ctx->mediacodec, ctx->packet.size, in_buffersize);
                    ret = AVERROR_EXTERNAL;
                    ctx->stats.in_fail_cnt ++;
                    break;
                }

                memcpy(in_buffer, ctx->packet.data, ctx->packet.size);
                in_bufidx = AMediaCodec_queueInputBuffer(ctx->mediacodec, in_bufidx, 0, ctx->packet.size, ctx->packet.pts, 0);
                ctx->in_pts = ctx->packet.pts;
                ctx->in_duration = ctx->packet.duration;
            } else {
                in_bufidx = AMediaCodec_queueInputBuffer(ctx->mediacodec, in_bufidx, 0, 0, 0, HLMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                ctx->in_pts += ctx->in_duration;
                hi_logi(avctx, "%s %d AMediaCodec_queueInputBuffer eof flush", __FUNCTION__, __LINE__);
            }

            if (in_bufidx != 0) {
                ret = AVERROR_EXTERNAL;
                ctx->stats.in_fail_cnt ++;
                hi_loge(avctx,"AMediaCodec_queueInputBuffer fail (%d)", in_bufidx);
            } else {
                ctx->stats.in_succ_cnt ++;
            }

            break;
        }
    } while (false);

    av_packet_unref(&ctx->packet);
    return ret;
}

static int hlmediacodec_dec_recv(AVCodecContext *avctx, AVFrame* frame) {
    HLMediaCodecDecContext *ctx = avctx->priv_data;

    int ret = 0;
    int ou_times = ctx->ou_timeout_times;
    int ou_timeout = ctx->in_eof ? ctx->eof_timeout : ctx->ou_timeout;

    while (true) {
        -- ou_times;

        AMediaCodecBufferInfo bufferInfo = {0};
        ssize_t ou_bufidx = AMediaCodec_dequeueOutputBuffer(ctx->mediacodec, &bufferInfo, ou_timeout);
        hi_logt(avctx, "%s %d AMediaCodec_dequeueOutputBuffer ret (%d) times: %d offset: %d size: %d pts: %llu flags: %u", __FUNCTION__, __LINE__, 
            ou_bufidx, ctx->ou_timeout_times - ou_times, bufferInfo.offset, bufferInfo.size, bufferInfo.presentationTimeUs, bufferInfo.flags);
        if (ou_bufidx >= 0) {
            ctx->stats.ou_succ_cnt ++;

            size_t ou_bufsize = 0;
            uint8_t* ou_buf = AMediaCodec_getOutputBuffer(ctx->mediacodec, ou_bufidx, &ou_bufsize);
            if (!ou_buf) {
                hi_loge(avctx, "%s %d AMediaCodec_getOutputBuffer codec: %p fail", __FUNCTION__, __LINE__, ctx->mediacodec);
                AMediaCodec_releaseOutputBuffer(ctx->mediacodec, ou_bufidx, false);
                ret = AVERROR_EXTERNAL;
                break;
            }

            if ((ret = ff_decode_frame_props(avctx, frame)) < 0) {
                hi_loge(avctx, "ff_decode_frame_props fail(%d)", ret);
                AMediaCodec_releaseOutputBuffer(ctx->mediacodec, ou_bufidx, false);
                ret = AVERROR_EXTERNAL;
                break;
            }

            if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                frame->width = avctx->width;
                frame->height = avctx->height;
            } else if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
                frame->nb_samples = avctx->frame_size;
            }

            if ((ret = ff_get_buffer(avctx, frame, 0))) {
                hi_loge(avctx, "ff_get_buffer fail(%d)", ret);
                AMediaCodec_releaseOutputBuffer(ctx->mediacodec, ou_bufidx, false);
                ret = AVERROR_EXTERNAL;
                break;
            }

            if (bufferInfo.size) {
                if (ctx->buffer_size < bufferInfo.size) {
                    if (ctx->buffer) {
                        av_free(ctx->buffer);
                        ctx->buffer = NULL;
                        ctx->buffer_size = 0;
                    }

                    ctx->buffer = av_malloc(bufferInfo.size);
                    ctx->buffer_size = bufferInfo.size;

                    hi_logi(avctx, "%s %d av_malloc %u", __FUNCTION__, __LINE__, ctx->buffer_size);
                }

                memcpy(ctx->buffer, ou_buf, bufferInfo.size);

                uint32_t remain_size = ctx->buffer_size - bufferInfo.size;
                if (remain_size > 0) {
                    memset(ctx->buffer + bufferInfo.size, 0, remain_size);
                }
            }

            AMediaCodec_releaseOutputBuffer(ctx->mediacodec, ou_bufidx, false);

            ctx->stats.ou_succ_frame_cnt ++;

            if (bufferInfo.flags & HLMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) { 
                ctx->stats.ou_succ_conf_cnt ++;
            }

            if (bufferInfo.flags & HLMEDIACODEC_CONFIGURE_FLAG_ENCODE) { 
                ctx->stats.ou_succ_idr_cnt ++;
            }

            frame->pts = frame->pkt_pts = frame->pkt_dts = bufferInfo.presentationTimeUs;
            frame->pkt_duration = ctx->in_duration;

            if (bufferInfo.flags & HLMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                frame->pts = frame->pkt_pts = frame->pkt_dts = ctx->in_pts;
                ctx->ou_eof = true;
                ctx->stats.ou_succ_end_cnt ++;
                hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer HLMEDIACODEC_BUFFER_FLAG_END_OF_STREAM", __FUNCTION__, __LINE__);
                ret = AVERROR_EOF;
                break;
            }

            ret = hlmediacodec_decode_buffer_to_frame(avctx, bufferInfo, frame);
            break;
        }

        ctx->stats.ou_fail_cnt ++;
        if (ou_bufidx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            hi_logd(avctx, "%s %d AMediaCodec_dequeueOutputBuffer AMEDIACODEC_INFO_TRY_AGAIN_LATER ", __FUNCTION__, __LINE__);

            ctx->stats.ou_fail_again_cnt ++;

            if (ou_times <= 0) {
                ret = AVERROR(EAGAIN);
                hi_loge(avctx, "%s %d AMediaCodec_dequeueOutputBuffer timeout ", __FUNCTION__, __LINE__);
                break;
            }

            continue;
        } else if (ou_bufidx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ", __FUNCTION__, __LINE__);

            ctx->stats.ou_fail_format_cnt ++;

            AMediaFormat* format = AMediaCodec_getOutputFormat(ctx->mediacodec);
            if (format) {
                hi_logi(avctx, "%s %d AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED %s", __FUNCTION__, __LINE__, AMediaFormat_toString(format));

                hlmediacodec_fill_context(format, avctx);
                AMediaFormat_delete(format);
            }

            continue;
        } else if (ou_bufidx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED", __FUNCTION__, __LINE__);

            ctx->stats.ou_fail_buffer_cnt ++;
            continue;
        } else {
            hi_loge(avctx, "%s %d AMediaCodec_dequeueOutputBuffer fail (%d)", __FUNCTION__, __LINE__, ou_bufidx);
            ctx->stats.ou_fail_oth_cnt ++;
            ret = AVERROR(EAGAIN);
            break;
        }
    }

    return ret;
}

static av_cold int hlmediacodec_receive_frame(AVCodecContext *avctx, AVFrame* frame) {
    HLMediaCodecDecContext *ctx = avctx->priv_data;
    if (!ctx->inited) {
        return AVERROR_EXTERNAL;
    }

    if (ctx->in_eof && ctx->ou_eof) {
        return AVERROR_EOF;
    }

    int ret = 0;
    if (!ctx->in_eof) {
        if ((ret = hlmediacodec_dec_send(avctx)) != 0) {
            return ret;
        }
    }

    return hlmediacodec_dec_recv(avctx, frame);
}

static av_cold void hlmediacodec_decode_flush(AVCodecContext *avctx) {
    hi_logi(avctx, "%s %d", __FUNCTION__, __LINE__);

    HLMediaCodecDecContext *ctx = avctx->priv_data;
    if (!ctx->inited) {
        return;
    }

    ctx->in_eof = false;
    ctx->ou_eof = false;
    ctx->inited = false;

    if (ctx->mediacodec) {
        AMediaCodec_flush(ctx->mediacodec);
        AMediaCodec_stop(ctx->mediacodec);
        AMediaCodec_delete(ctx->mediacodec);
        ctx->mediacodec = NULL;
    }

    const char* mime = ff_hlmediacodec_get_mime(avctx->codec_id);
    if (!mime) {
        hi_loge(avctx, "ff_hlmediacodec_get_mime fail (%d)", avctx->codec_id);
        return;
    }

    if (!(ctx->mediacodec = AMediaCodec_createDecoderByType(mime))) {
        hi_loge(avctx, "AMediaCodec_createDecoderByType fail (%s)", mime);
        return;
    }

    media_status_t status = AMEDIA_OK;
    if ((status = AMediaCodec_configure(ctx->mediacodec, ctx->mediaformat, NULL, NULL, 0))) {
        hi_loge(avctx, "AMediaCodec_configure fail (%d)", status);
        return;
    }

    if ((status = AMediaCodec_start(ctx->mediacodec))) {
        hi_loge(avctx, "AMediaCodec_start fail (%d)", status);
        return;
    }

    ctx->inited = true;
}

static av_cold int hlmediacodec_decode_close(AVCodecContext *avctx) {
    hi_logi(avctx, "%s %d", __FUNCTION__, __LINE__);

    HLMediaCodecDecContext *ctx = avctx->priv_data;
    ctx->stats.uint_stamp = av_gettime_relative();
    hlmediacodec_show_stats(avctx, ctx->stats);
   
    if (ctx->mediacodec) {
        AMediaCodec_flush(ctx->mediacodec);
        AMediaCodec_stop(ctx->mediacodec);
        AMediaCodec_delete(ctx->mediacodec);
        ctx->mediacodec = NULL;
    }

    if (ctx->mediaformat) {
        AMediaFormat_delete(ctx->mediaformat);
        ctx->mediaformat = NULL;
    }

    av_packet_unref(&ctx->packet);

    if (ctx->buffer) {
        av_free(ctx->buffer);
        ctx->buffer = NULL;
        ctx->buffer_size = 0;
    }

    ctx->inited = false;
    return 0;
}


static const AVCodecHWConfigInternal *const hlmediacodec_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public          = {
            .pix_fmt     = AV_PIX_FMT_MEDIACODEC,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_AD_HOC |
                           AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_MEDIACODEC,
        },
        .hwaccel         = NULL,
    },
    NULL
};

#define OFFSET(x) offsetof(HLMediaCodecDecContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption ff_hlmediacodec_dec_options[] = {
    { "in_timeout", "in buff timeout", OFFSET(in_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_IN_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VD },
    { "ou_timeout", "ou buff timeout", OFFSET(ou_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_OU_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VD },
    { "eof_timeout", "eof buff timeout", OFFSET(eof_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_EOF_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VD },
    { "in_timeout_times", "in buff timeout times", OFFSET(in_timeout_times), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_IN_SET_TIMEOUT_TIMES}, HLMEDIACODEC_MIN_TIMEOUT_TIMES, HLMEDIACODEC_MAX_TIMEOUT_TIMES, VD },
    { "ou_timeout_times", "ou buff timeout times", OFFSET(ou_timeout_times), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_DEC_OU_SET_TIMEOUT_TIMES}, HLMEDIACODEC_MIN_TIMEOUT_TIMES, HLMEDIACODEC_MAX_TIMEOUT_TIMES, VD },
    { NULL }
};

#define DECLARE_HLMEDIACODEC_VCLASS(short_name)                   \
static const AVClass ff_##short_name##_hlmediacodec_dec_class = { \
    .class_name = #short_name "_hlmediacodec",                    \
    .item_name  = av_default_item_name,                         \
    .option     = ff_hlmediacodec_dec_options,                   \
    .version    = LIBAVUTIL_VERSION_INT,                        \
};

#define DECLARE_HLMEDIACODEC_DEC(short_name, full_name, codec_id, codec_type, bsf)                         \
DECLARE_HLMEDIACODEC_VCLASS(short_name)                                                         \
AVCodec ff_##short_name##_hlmediacodec_decoder = {                                              \
    .name           = #short_name "_hlmediacodec",                                              \
    .long_name      = full_name " (Ffmpeg MediaCodec NDK)",            \
    .type           = codec_type,                                                       \
    .id             = codec_id,                                                                 \
    .priv_class     = &ff_##short_name##_hlmediacodec_dec_class,                                \
    .priv_data_size = sizeof(HLMediaCodecDecContext),                                       \
    .init           = hlmediacodec_decode_init,                                                   \
    .receive_frame  = hlmediacodec_receive_frame,                                                 \
    .flush          = hlmediacodec_decode_flush,                                                  \
    .close          = hlmediacodec_decode_close,                                                  \
    .capabilities   = AV_CODEC_CAP_DELAY | FF_CODEC_CAP_INIT_CLEANUP | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HARDWARE,                           \
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,                                               \
    .bsfs           = bsf,                                                                      \
    .hw_configs     = hlmediacodec_hw_configs,                                                   \
    .wrapper_name   = "hlmediacodec",                                                           \
};                                                                                              \

#ifdef CONFIG_H264_HLMEDIACODEC_DECODER
DECLARE_HLMEDIACODEC_DEC(h264, "H.264", AV_CODEC_ID_H264, AVMEDIA_TYPE_VIDEO, "h264_mp4toannexb")
#endif

#ifdef CONFIG_HEVC_HLMEDIACODEC_DECODER
DECLARE_HLMEDIACODEC_DEC(hevc, "H.265", AV_CODEC_ID_HEVC, AVMEDIA_TYPE_VIDEO, "hevc_mp4toannexb")
#endif

#ifdef CONFIG_MPEG4_HLMEDIACODEC_DECODER
DECLARE_HLMEDIACODEC_DEC(mpeg4, "MPEG-4", AV_CODEC_ID_MPEG4, AVMEDIA_TYPE_VIDEO, NULL)
#endif

#ifdef CONFIG_VP8_HLMEDIACODEC_DECODER
DECLARE_HLMEDIACODEC_DEC(vp8, "VP8", AV_CODEC_ID_VP8, AVMEDIA_TYPE_VIDEO, NULL)
#endif

#ifdef CONFIG_VP9_HLMEDIACODEC_DECODER
DECLARE_HLMEDIACODEC_DEC(vp9, "VP9", AV_CODEC_ID_VP9, AVMEDIA_TYPE_VIDEO, NULL)
#endif

#ifdef CONFIG_AAC_HLMEDIACODEC_DECODER
DECLARE_HLMEDIACODEC_DEC(aac, "AAC", AV_CODEC_ID_AAC, AVMEDIA_TYPE_AUDIO, "aac_adtstoasc")
#endif

#ifdef CONFIG_MP3_HLMEDIACODEC_DECODER
DECLARE_HLMEDIACODEC_DEC(mp3, "MP3", AV_CODEC_ID_MP3, AVMEDIA_TYPE_AUDIO, NULL)
#endif
