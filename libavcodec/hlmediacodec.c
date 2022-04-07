#include "hlmediacodec.h"
#include <string.h>
#include <sys/types.h>

#include "libavutil/frame.h"
#include "libavutil/mem.h"

#define HLMEDIACODEC_LOG_TAG		"[hlmediacodec]"

static const struct {
    enum AVPixelFormat pix_fmt;
    enum FFHlMediaCodecColorFormat color_fmt;
} pix_fmt_map[] = {
    { AV_PIX_FMT_NV12,     COLOR_FormatYUV420SemiPlanar },
    { AV_PIX_FMT_YUV420P,  COLOR_FormatYUV420Planar },
    { AV_PIX_FMT_YUV422P,  COLOR_FormatYUV422Flexible },
    { AV_PIX_FMT_YUV444P,  COLOR_FormatYUV444Flexible },
    { AV_PIX_FMT_RGB8,     COLOR_FormatRGBFlexible },
    { AV_PIX_FMT_BGR24,    COLOR_Format24bitBGR888 },
    { AV_PIX_FMT_ABGR,     COLOR_Format32bitABGR8888 },
    { AV_PIX_FMT_RGBA,     COLOR_FormatRGBAFlexible },
    { AV_PIX_FMT_RGB565BE, COLOR_Format16bitRGB565 },
    { AV_PIX_FMT_NONE,     COLOR_FormatSurface },
};

static const struct {
    enum AVSampleFormat sample_fmt;
    enum FFHlMediaCodecPcmFormat pcm_fmt;
} pcm_fmt_map[] = {
    { AV_SAMPLE_FMT_U8, HLMEDIACODEC_PCM_8BIT },
    { AV_SAMPLE_FMT_S16, HLMEDIACODEC_PCM_16BIT },
    { AV_SAMPLE_FMT_S32, HLMEDIACODEC_PCM_FLOAT },
    { AV_SAMPLE_FMT_U8P, HLMEDIACODEC_PCM_8BIT },
    { AV_SAMPLE_FMT_S16P, HLMEDIACODEC_PCM_16BIT },
    { AV_SAMPLE_FMT_S32P, HLMEDIACODEC_PCM_FLOAT },
};

enum FFHlMediaCodecPcmFormat ff_hlmediacodec_get_pcm_format(enum AVSampleFormat sample_fmt) {
    unsigned i = 0;
    for (i = 0; pcm_fmt_map[i].sample_fmt != AV_SAMPLE_FMT_NONE; i++) {
        if (pcm_fmt_map[i].sample_fmt == sample_fmt)
            return pcm_fmt_map[i].pcm_fmt;
    }

    return HLMEDIACODEC_PCM_16BIT;
}

enum AVSampleFormat ff_hlmediacodec_get_sample_fmt(enum FFHlMediaCodecPcmFormat pcm_fmt) {
    unsigned i = 0;
    for (i = 0; pcm_fmt_map[i].sample_fmt != AV_SAMPLE_FMT_NONE; i++) {
        if (pcm_fmt_map[i].pcm_fmt == pcm_fmt)
            return pcm_fmt_map[i].sample_fmt;
    }
    return AV_SAMPLE_FMT_NONE;
}

enum FFHlMediaCodecColorFormat ff_hlmediacodec_get_color_format(enum AVPixelFormat pix_fmt) {
    unsigned i;
    for (i = 0; pix_fmt_map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (pix_fmt_map[i].pix_fmt == pix_fmt)
            return pix_fmt_map[i].color_fmt;
    }
    return COLOR_FormatSurface;
}

enum AVPixelFormat ff_hlmediacodec_get_pix_fmt(enum FFHlMediaCodecColorFormat color_fmt) {
    unsigned i;
    for (i = 0; pix_fmt_map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (pix_fmt_map[i].color_fmt == color_fmt)
            return pix_fmt_map[i].pix_fmt;
    }
    return AV_PIX_FMT_NONE;
}

const char* ff_hlmediacodec_get_mime(enum AVCodecID codec_id) {
    switch (codec_id) {
    case AV_CODEC_ID_H264:
        return "video/avc";
    case AV_CODEC_ID_HEVC:
        return "video/hevc";
    case AV_CODEC_ID_MPEG2VIDEO:
        return "video/mpeg2";
    case AV_CODEC_ID_MPEG4:
        return "video/mp4v-es";
    case AV_CODEC_ID_VP8:
        return "video/x-vnd.on2.vp8";
    case AV_CODEC_ID_VP9:
        return "video/x-vnd.on2.vp9";
    case AV_CODEC_ID_AAC:
        return "audio/mp4a-latm";
    case AV_CODEC_ID_MP3:
        return "audio/mpeg";
    default:
        return NULL;
    }
}

#define kMediaCodecLogBuffUseSize 2048
#define kMediaCodecLogBuffSize (kMediaCodecLogBuffUseSize + 1)

static void hi_log(void* ctx, int lvl, const char* fmt, va_list args) {
    char logbuf[kMediaCodecLogBuffUseSize] = {0};

    vsnprintf(logbuf, kMediaCodecLogBuffUseSize, fmt, args);

    av_log(ctx, lvl, "%s %s \n", HLMEDIACODEC_LOG_TAG, logbuf);
}

void hi_loge(void* ctx, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  hi_log(ctx, AV_LOG_ERROR, fmt, args);
  va_end(args);
}

void hi_logw(void* ctx, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  hi_log(ctx, AV_LOG_WARNING, fmt, args);
  va_end(args);
}

void hi_logi(void* ctx, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  hi_log(ctx, AV_LOG_INFO, fmt, args);
  va_end(args);
}

void hi_logd(void* ctx, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  hi_log(ctx, AV_LOG_DEBUG, fmt, args);
  va_end(args);
}

void hi_logt(void* ctx, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  hi_log(ctx, AV_LOG_TRACE, fmt, args);
  va_end(args);
}
