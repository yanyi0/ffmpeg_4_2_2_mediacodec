#include "hlmediacodec_codec.h"
#include "h264_parse.h"
#include "hevc_parse.h"
#include <string.h>
#include <sys/types.h>
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"
#include "libavcodec/mpeg4audio.h"

static const int hl_mpeg4audio_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

static int hl_h2645_ps_to_nalu(const uint8_t *src, int src_size, uint8_t **out, int *out_size)
{
    int i;
    int ret = 0;
    uint8_t *p = NULL;
    static const uint8_t nalu_header[] = { 0x00, 0x00, 0x00, 0x01 };

    if (!out || !out_size) {
        return AVERROR(EINVAL);
    }

    p = av_malloc(sizeof(nalu_header) + src_size);
    if (!p) {
        return AVERROR(ENOMEM);
    }

    *out = p;
    *out_size = sizeof(nalu_header) + src_size;

    memcpy(p, nalu_header, sizeof(nalu_header));
    memcpy(p + sizeof(nalu_header), src, src_size);

    /* Escape 0x00, 0x00, 0x0{0-3} pattern */
    for (i = 4; i < *out_size; i++) {
        if (i < *out_size - 3 &&
            p[i + 0] == 0 &&
            p[i + 1] == 0 &&
            p[i + 2] <= 3) {
            uint8_t *new;

            *out_size += 1;
            new = av_realloc(*out, *out_size);
            if (!new) {
                ret = AVERROR(ENOMEM);
                goto done;
            }
            *out = p = new;

            i = i + 2;
            memmove(p + i + 1, p + i, *out_size - (i + 1));
            p[i] = 0x03;
        }
    }
done:
    if (ret < 0) {
        av_freep(out);
        *out_size = 0;
    }

    return ret;
}

static int hl_h264_set_extradata(AVCodecContext *avctx, AMediaFormat *format)
{
    int i;
    int ret;

    H264ParamSets ps;
    const PPS *pps = NULL;
    const SPS *sps = NULL;
    int is_avc = 0;
    int nal_length_size = 0;

    memset(&ps, 0, sizeof(ps));

    ret = ff_h264_decode_extradata(avctx->extradata, avctx->extradata_size,
                                   &ps, &is_avc, &nal_length_size, 0, avctx);
    if (ret < 0) {
        goto done;
    }

    for (i = 0; i < MAX_PPS_COUNT; i++) {
        if (ps.pps_list[i]) {
            pps = (const PPS*)ps.pps_list[i]->data;
            break;
        }
    }

    if (pps) {
        if (ps.sps_list[pps->sps_id]) {
            sps = (const SPS*)ps.sps_list[pps->sps_id]->data;
        }
    }

    if (pps && sps) {
        uint8_t *data = NULL;
        int data_size = 0;

        if ((ret = hl_h2645_ps_to_nalu(sps->data, sps->data_size, &data, &data_size)) < 0) {
            goto done;
        }
        AMediaFormat_setBuffer(format, "csd-0", (void*)data, data_size);
        av_freep(&data);

        if ((ret = hl_h2645_ps_to_nalu(pps->data, pps->data_size, &data, &data_size)) < 0) {
            goto done;
        }
        AMediaFormat_setBuffer(format, "csd-1", (void*)data, data_size);
        av_freep(&data);
    } else {
        av_log(avctx, AV_LOG_ERROR, "Could not extract PPS/SPS from extradata");
        ret = AVERROR_INVALIDDATA;
    }

done:
    ff_h264_ps_uninit(&ps);

    return ret;
}

static int hl_hevc_set_extradata(AVCodecContext *avctx, AMediaFormat *format)
{
    int i;
    int ret;

    HEVCParamSets ps;
    HEVCSEI sei;

    const HEVCVPS *vps = NULL;
    const HEVCPPS *pps = NULL;
    const HEVCSPS *sps = NULL;
    int is_nalff = 0;
    int nal_length_size = 0;

    uint8_t *vps_data = NULL;
    uint8_t *sps_data = NULL;
    uint8_t *pps_data = NULL;
    int vps_data_size = 0;
    int sps_data_size = 0;
    int pps_data_size = 0;

    memset(&ps, 0, sizeof(ps));
    memset(&sei, 0, sizeof(sei));

    ret = ff_hevc_decode_extradata(avctx->extradata, avctx->extradata_size,
                                   &ps, &sei, &is_nalff, &nal_length_size, 0, 1, avctx);
    if (ret < 0) {
        goto done;
    }

    for (i = 0; i < HEVC_MAX_VPS_COUNT; i++) {
        if (ps.vps_list[i]) {
            vps = (const HEVCVPS*)ps.vps_list[i]->data;
            break;
        }
    }

    for (i = 0; i < HEVC_MAX_PPS_COUNT; i++) {
        if (ps.pps_list[i]) {
            pps = (const HEVCPPS*)ps.pps_list[i]->data;
            break;
        }
    }

    if (pps) {
        if (ps.sps_list[pps->sps_id]) {
            sps = (const HEVCSPS*)ps.sps_list[pps->sps_id]->data;
        }
    }

    if (vps && pps && sps) {
        uint8_t *data;
        int data_size;

        if ((ret = hl_h2645_ps_to_nalu(vps->data, vps->data_size, &vps_data, &vps_data_size)) < 0 ||
            (ret = hl_h2645_ps_to_nalu(sps->data, sps->data_size, &sps_data, &sps_data_size)) < 0 ||
            (ret = hl_h2645_ps_to_nalu(pps->data, pps->data_size, &pps_data, &pps_data_size)) < 0) {
            goto done;
        }

        data_size = vps_data_size + sps_data_size + pps_data_size;
        data = av_mallocz(data_size);
        if (!data) {
            ret = AVERROR(ENOMEM);
            goto done;
        }

        memcpy(data                                , vps_data, vps_data_size);
        memcpy(data + vps_data_size                , sps_data, sps_data_size);
        memcpy(data + vps_data_size + sps_data_size, pps_data, pps_data_size);

        AMediaFormat_setBuffer(format, "csd-0", data, data_size);

        av_freep(&data);
    } else {
        av_log(avctx, AV_LOG_ERROR, "Could not extract VPS/PPS/SPS from extradata");
        ret = AVERROR_INVALIDDATA;
    }

done:
    ff_hevc_ps_uninit(&ps);

    av_freep(&vps_data);
    av_freep(&sps_data);
    av_freep(&pps_data);

    return ret;
}

static int hl_aac_set_extradata(AVCodecContext *avctx, AMediaFormat *format) {
	int sampling_index = 0;
	for(; sampling_index < sizeof(hl_mpeg4audio_sample_rates); sampling_index ++) {
		if (hl_mpeg4audio_sample_rates[sampling_index] == avctx->sample_rate) {
            break;
		}
	}

	if(sampling_index == sizeof(hl_mpeg4audio_sample_rates)) {
	    sampling_index = 4; //default
	}

	uint16_t val =  (0xFFFF & (AOT_AAC_LC << 11)) |
					(0xFFFF & (sampling_index << 7)) |
					(0xFFFF & (avctx->channels << 3) );

    uint8_t esds[2] = {0};
	esds[0] = (val >> 8);
	esds[1] = (val);

    AMediaFormat_setBuffer(format, "csd-0", esds, sizeof(esds));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_AAC_PROFILE, avctx->profile);

    hi_logi(avctx, "%s samplerate = %d, channels = %d, csd-0 = %02X%02X", __FUNCTION__, avctx->sample_rate, avctx->channels, esds[0], esds[1]);
    return 0;
}

static int hl_common_set_extradata(AVCodecContext *avctx, AMediaFormat *format) {
    int ret = 0;

    if (avctx->extradata) {
        AMediaFormat_setBuffer(format, "csd-0", avctx->extradata, avctx->extradata_size);
    }

    return ret;
}


static int hlmediacodec_decode_fill_format(AVCodecContext* avctx, AMediaFormat* mediaformat) {
  hi_logd(avctx, "%s %d", __FUNCTION__, __LINE__);

  int ret = AVERROR_EXTERNAL;
  HLMediaCodecDecContext* ctx = avctx->priv_data;

  do {
    const char* mime = ff_hlmediacodec_get_mime(avctx->codec_id);
    if (!mime) {
      hi_loge(avctx, "%s %d codec (%d) unsupport!", __FUNCTION__, __LINE__, avctx->codec_id);
      break;
    }

    AMediaFormat_setString(mediaformat, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_BIT_RATE, avctx->bit_rate);
    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        uint32_t frame_max_size = av_samples_get_buffer_size(NULL, avctx->channels, avctx->sample_rate, avctx->sample_fmt, 1);
        AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, frame_max_size);
        AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_SAMPLE_RATE, avctx->sample_rate);
        AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, avctx->channels);
    } else if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_WIDTH, avctx->width);
        AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_HEIGHT, avctx->height);
    }

    switch (avctx->codec_id) {
        case AV_CODEC_ID_H264:
            ret = hl_h264_set_extradata(avctx, mediaformat);
            break;
        case AV_CODEC_ID_HEVC:
            ret = hl_hevc_set_extradata(avctx, mediaformat);
            break;
        case AV_CODEC_ID_AAC:
            ret = hl_aac_set_extradata(avctx, mediaformat);
            break;
        default:
            ret = hl_common_set_extradata(avctx, mediaformat);
        break;
    }

    if (ret != 0) {
        break;
    }

    hi_logi(avctx, "%s %d mime: %s timeout: (in: %d ou: %d) times: (in: %d ou: %d) "
            "width: %d height: %d pix_fmt: %d sample_fmt: %d", __FUNCTION__, __LINE__, mime,
            ctx->in_timeout, ctx->ou_timeout, ctx->in_timeout_times, ctx->ou_timeout_times,
            avctx->width, avctx->height, avctx->pix_fmt, avctx->sample_fmt);

    ret = 0;
  } while (false);

  return ret;
}

static int hlmediacodec_encode_fill_format(AVCodecContext* avctx, AMediaFormat* mediaformat) {
  hi_logd(avctx, "%s %d", __FUNCTION__, __LINE__);

  int ret = AVERROR_EXTERNAL;
  HLMediaCodecEncContext* ctx = avctx->priv_data;

  do {
    const char* mime = ff_hlmediacodec_get_mime(avctx->codec_id);
    if (!mime) {
        hi_loge(avctx, "%s %d codec (%d) unsupport!", __FUNCTION__, __LINE__, avctx->codec_id);
        break;
    }

    int color_format = ff_hlmediacodec_get_color_format(avctx->pix_fmt);

    AMediaFormat_setString(mediaformat, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_HEIGHT, avctx->height);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_WIDTH, avctx->width);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_BIT_RATE, avctx->bit_rate);
    AMediaFormat_setFloat(mediaformat, AMEDIAFORMAT_KEY_FRAME_RATE, av_q2d(avctx->framerate));
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
    AMediaFormat_setInt32(mediaformat, AMEDIAFORMAT_KEY_COLOR_FORMAT, color_format);
    AMediaFormat_setInt32(mediaformat, "bitrate-mode", ctx->rc_mode);//质量优先

    hi_logi(avctx, "%s %d mime: %s timeout: (in: %d ou: %d) times: (in: %d ou: %d) "
            "width: %d height: %d pix_fmt: %d", __FUNCTION__, __LINE__, mime,
            ctx->in_timeout, ctx->ou_timeout, ctx->in_timeout_times, ctx->ou_timeout_times,
            avctx->width, avctx->height, avctx->pix_fmt);

    ret = 0;
  } while (false);

  hi_logi(avctx, "%s %d end (%d)", __FUNCTION__, __LINE__, ret);

  return ret;
}

int hlmediacodec_fill_format(AVCodecContext* avctx, AMediaFormat* mediaformat) {
  if (av_codec_is_decoder(avctx->codec)) {
    return hlmediacodec_decode_fill_format(avctx, mediaformat);
  } else if (av_codec_is_encoder(avctx->codec)) {
    return hlmediacodec_encode_fill_format(avctx, mediaformat);
  } else {
    return AVERROR_PROTOCOL_NOT_FOUND;
  }
}

int hlmediacodec_fill_context(AMediaFormat* mediaformat, AVCodecContext* avctx) {
    if (av_codec_is_decoder(avctx->codec)) {
        if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            int media_format = 0;
            int media_samplerate = 0;
            int media_channel = 0;
            int media_aac_profile = 0;

            AMediaFormat_getInt32(mediaformat, "pcm-encoding", &media_format);
            AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &media_samplerate);
            AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &media_channel);
            AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_AAC_PROFILE, &media_aac_profile);

            hi_logi(avctx, "%s %d mediacodec (format: %d samplerate: %d channel: %d aacprofile: %d)", __FUNCTION__, __LINE__, media_format, media_samplerate, media_channel, media_aac_profile);

            if (!media_format) {
                media_format = HLMEDIACODEC_PCM_16BIT;
            }

            avctx->sample_fmt = ff_hlmediacodec_get_sample_fmt((enum FFHlMediaCodecPcmFormat)media_format);

            if (media_samplerate) {
                avctx->sample_rate = media_samplerate;
            }

            if (media_channel) {
                avctx->channels = media_channel;
            }
        } else if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            int media_format = 0;
            int media_width = 0;
            int media_height = 0;
            int media_stride = 0;
            int media_crop_top = 0;
            int media_crop_bottom = 0;
            int media_crop_left = 0;
            int media_crop_right = 0;
            int media_slice_height = 0;

            AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_COLOR_FORMAT, &media_format);
            AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_WIDTH, &media_width);
            AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_HEIGHT, &media_height);
            AMediaFormat_getInt32(mediaformat, AMEDIAFORMAT_KEY_STRIDE, &media_stride);
            AMediaFormat_getInt32(mediaformat, "crop-top", &media_crop_top);
            AMediaFormat_getInt32(mediaformat, "crop-bottom", &media_crop_bottom);
            AMediaFormat_getInt32(mediaformat, "crop-left", &media_crop_left);
            AMediaFormat_getInt32(mediaformat, "crop-right", &media_crop_right);
            AMediaFormat_getInt32(mediaformat, "slice-height", &media_slice_height);

            hi_logi(avctx, "%s %d mediacodec (format: %d size: [w: %d h: %d s: %d] crop: [t: %d b: %d l: %d r: %d] sliceheight: %d)",
                __FUNCTION__, __LINE__, media_format, media_width, media_height, media_stride, media_crop_top, media_crop_bottom, media_crop_left, media_crop_right, media_slice_height);

            if (media_format) {
                avctx->pix_fmt = ff_hlmediacodec_get_pix_fmt((enum FFHlMediaCodecColorFormat)media_format);
            }

            if (media_width && media_height) {
                ff_set_dimensions(avctx, media_width, media_height);
            }
        }
    }

    return 0;
}

int hlmediacodec_decode_buffer_to_frame(AVCodecContext* avctx, AMediaCodecBufferInfo bufferinfo, AVFrame* frame) {
  HLMediaCodecDecContext *ctx = avctx->priv_data;
  int ret = 0;

    do {
        if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            uint32_t frame_data_size = av_samples_get_buffer_size(NULL, frame->channels, frame->nb_samples, (enum AVSampleFormat)frame->format, 1);

            if (bufferinfo.size != frame_data_size) {
                hi_logw(avctx, "%s %d data size unmatch (%u %u)", __FUNCTION__, __LINE__, bufferinfo.size, frame_data_size);
            }

            if (ctx->buffer_size < frame_data_size) {
                ret = AVERROR_EXTERNAL;
                hi_loge(avctx, "%s %d buff size unmatch (%u %u)", __FUNCTION__, __LINE__, ctx->buffer_size, frame_data_size);
                break;
            }

            if ((ret = avcodec_fill_audio_frame(frame, frame->channels, (enum AVSampleFormat)frame->format, ctx->buffer, frame_data_size, 1)) < 0) {
                hi_loge(avctx, "%s %d avcodec_fill_audio_frame fail (%d) format: %d channels: %d format: %d", __FUNCTION__, __LINE__, ret, frame->format, frame->channels, frame->format);
                break;
            }
        } else if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            uint32_t frame_data_size = av_image_get_buffer_size((enum AVPixelFormat)frame->format, frame->width, frame->height, 1);

            if (bufferinfo.size != frame_data_size) {
                hi_logw(avctx, "%s %d data size unmatch (%u %u)", __FUNCTION__, __LINE__, bufferinfo.size, frame_data_size);
            }

            if (ctx->buffer_size < frame_data_size) {
                ret = AVERROR_EXTERNAL;
                hi_loge(avctx, "%s %d buff size unmatch (%u %u)", __FUNCTION__, __LINE__, ctx->buffer_size, frame_data_size);
                break;
            }

            if ((ret = av_image_fill_linesizes(frame->linesize, (enum AVPixelFormat)frame->format, frame->width)) < 0) {
                hi_loge(avctx, "%s %d av_image_fill_linesizes fail (%d) format: %d width: %d height: %d", __FUNCTION__, __LINE__, ret, frame->format, frame->width, frame->height);
                break;
            }

            if ((ret = av_image_fill_arrays(frame->data, frame->linesize, ctx->buffer, (enum AVPixelFormat)frame->format,
                                            frame->width, frame->height, 1)) < 0) {
                hi_loge(avctx, "%s %d av_image_fill_arrays fail (%d) format: %d width: %d height: %d", __FUNCTION__, __LINE__, ret, frame->format, frame->width, frame->height);
                break;
            }
        }
    } while (false);

    return ret;
}

int hlmediacodec_encode_header(AVCodecContext* avctx) {
    HLMediaCodecEncContext *ctx = avctx->priv_data;
    AMediaCodec* codec = NULL;
    int ret = AVERROR_BUG;

    do {
        hi_logi(avctx, "%s %d start", __FUNCTION__, __LINE__);

        const char* mime = ff_hlmediacodec_get_mime(avctx->codec_id);
        if (!mime) {
            hi_loge(avctx, "%s %d codec (%d) unsupport!", __FUNCTION__, __LINE__, avctx->codec_id);
            break;
        }

        hi_logi(avctx, "%s %d AMediaCodec_createEncoderByType %s", __FUNCTION__, __LINE__, mime);

        if (!(codec = AMediaCodec_createEncoderByType(mime))) {
            hi_loge(avctx, "%s %d AMediaCodec_createEncoderByType (%s) failed!", __FUNCTION__, __LINE__, mime);
            break;
        }

        hi_logi(avctx, "%s %d AMediaCodec_configure %s format %s", __FUNCTION__, __LINE__, mime, AMediaFormat_toString(ctx->mediaformat));

        media_status_t status = AMEDIA_OK;
        if ((status = AMediaCodec_configure(codec, ctx->mediaformat, NULL, 0, HLMEDIACODEC_CONFIGURE_FLAG_ENCODE)) != AMEDIA_OK) {
            hi_loge(avctx, "%s %d AMediaCodec_configure failed (%d)!", __FUNCTION__, __LINE__, status);
            break;
        }

        if ((status = AMediaCodec_start(codec))) {
            hi_loge(avctx, "%s %d AMediaCodec_start failed (%d)!", __FUNCTION__, __LINE__, status);
            break;
        }

        int in_times = ctx->in_timeout_times;
        while (true) {//input buff
            ssize_t bufferIndex = AMediaCodec_dequeueInputBuffer(codec, ctx->in_timeout);
            if (bufferIndex < 0) {
                hi_loge(avctx, "%s %d AMediaCodec_dequeueInputBuffer failed (%d) times: %d!", __FUNCTION__, __LINE__, bufferIndex, in_times);

                if (in_times -- <= 0) {
                    hi_loge(avctx, "%s %d AMediaCodec_dequeueInputBuffer timeout ", __FUNCTION__, __LINE__);
                    break;
                }

                continue;
            }

            size_t bufferSize = 0;
            uint8_t* buffer = AMediaCodec_getInputBuffer(codec, bufferIndex, &bufferSize);
            if (!buffer) {
                hi_loge(avctx, "%s %d AMediaCodec_getInputBuffer failed!", __FUNCTION__, __LINE__);
                break;
            }

            int status = AMediaCodec_queueInputBuffer(codec, bufferIndex, 0, bufferSize, 0, 0);
            hi_logi(avctx, "%s %d AMediaCodec_queueInputBuffer status (%d)!", __FUNCTION__, __LINE__, status);
            break;
        }

        int ou_times = 5;
        bool got_config = false;

        while (!got_config) {
            AMediaCodecBufferInfo bufferInfo;
            int bufferIndex = AMediaCodec_dequeueOutputBuffer(codec, &bufferInfo, ctx->ou_timeout);
            hi_logi(avctx, "%s %d AMediaCodec_dequeueOutputBuffer stats (%d) size: %u offset: %u flags: %u pts: %lld", __FUNCTION__, __LINE__,
                bufferIndex, bufferInfo.size, bufferInfo.offset, bufferInfo.flags, bufferInfo.presentationTimeUs);
            if (bufferIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED || bufferIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                continue;
            } else if (bufferIndex < 0) {
                if (ou_times -- <= 0) {
                    hi_loge(avctx, "%s %d Got extradata timeout ", __FUNCTION__, __LINE__);
                    break;
                }

                continue;
            }

            size_t out_size = 0;
            uint8_t* out_buffer = AMediaCodec_getOutputBuffer(codec, bufferIndex, &out_size);
            if (!out_buffer) {
                hi_loge(avctx, "%s %d AMediaCodec_getOutputBuffer failed!", __FUNCTION__, __LINE__);
                AMediaCodec_releaseOutputBuffer(codec, bufferIndex, false);
                break;
            }

            if (bufferInfo.flags & HLMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                hi_logi(avctx, "%s %d Got extradata of size (%d %d)", __FUNCTION__, __LINE__, out_size, bufferInfo.size);

                if (avctx->extradata) {
                    av_freep(&avctx->extradata);
                    avctx->extradata = NULL;
                    avctx->extradata_size = 0;
                }

                avctx->extradata = av_mallocz(bufferInfo.size + AV_INPUT_BUFFER_PADDING_SIZE);
                avctx->extradata_size = bufferInfo.size;
                memcpy(avctx->extradata, out_buffer, avctx->extradata_size);

                got_config = true;
            }

            AMediaCodec_releaseOutputBuffer(codec, bufferIndex, false);
        }

        if (!got_config) {
            hi_loge(avctx, "%s %d get config fail!", __FUNCTION__, __LINE__);
            break;
        }

        ret = 0;
    } while (false);

    if (codec) {
        AMediaCodec_flush(codec);
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);

        hi_logi(avctx, "%s %d AMediaCodec_delete!", __FUNCTION__, __LINE__);
    }

    hi_logi(avctx, "%s %d ret: %d", __FUNCTION__, __LINE__, ret);
    return ret;
}

int hlmediacodec_get_buffer_size(AVCodecContext* avctx) {
    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        return av_samples_get_buffer_size(NULL, avctx->channels, avctx->frame_size, avctx->sample_fmt, 1);
    } else if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        return av_image_get_buffer_size(avctx->pix_fmt, avctx->width, avctx->height, 1);
    } else {
        return 0;
    }
}

void hlmediacodec_show_stats(AVCodecContext* avctx, HLMediaCodecStats stats) {
     hi_logi(avctx, "%s %d alive: %lld get: (succ: %d fail: %d) in: (succ: %d fail: [%d %d]) ou: [succ: (%d frame: [%d %d %d %d]) fail: (%d %d %d %d %d)]", __FUNCTION__, __LINE__,
        stats.uint_stamp - stats.init_stamp, stats.get_succ_cnt, stats.get_fail_cnt, stats.in_succ_cnt, stats.in_fail_cnt, stats.in_fail_again_cnt,
        stats.ou_succ_cnt, stats.ou_succ_frame_cnt, stats.ou_succ_conf_cnt, stats.ou_succ_idr_cnt, stats.ou_succ_end_cnt,
        stats.ou_fail_cnt, stats.ou_fail_again_cnt, stats.ou_fail_format_cnt, stats.ou_fail_buffer_cnt, stats.ou_fail_oth_cnt);

}
