#ifndef AVCODEC_HLMEDIACODEC_H
#define AVCODEC_HLMEDIACODEC_H
#include "libavcodec/avcodec.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"
#include "libavutil/avutil.h"
#include "libavutil/samplefmt.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
// #include "/Users/zego/Library/android-ndk-r20b/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/include/media/NdkMediaCodec.h"
// #include "/Users/zego/Library/android-ndk-r20b/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/include/media/NdkMediaFormat.h"

enum FFHlMediaCodecColorFormat {
    COLOR_FormatYUV420Planar                              = 0x13,
    COLOR_FormatYUV420PackedPlanar                        = 0x14,
    COLOR_FormatYUV420SemiPlanar                          = 0x15,
    COLOR_FormatYCbYCr                                    = 0x19,
    COLOR_FormatYUV420PackedSemiPlanar                    = 0x27,
    COLOR_FormatAndroidOpaque                             = 0x7F000789,
    COLOR_FormatYUV422Flexible                            = 0x7f422888,
    COLOR_FormatYUV444Flexible                            = 0x7f444888,
    COLOR_QCOM_FormatYUV420SemiPlanar                     = 0x7fa30c00,
    COLOR_QCOM_FormatYUV420SemiPlanar32m                  = 0x7fa30c04,
    COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka = 0x7fa30c03,
    COLOR_TI_FormatYUV420PackedSemiPlanar                 = 0x7f000100,
    COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced       = 0x7f000001,
    COLOR_FormatRGBFlexible                               = 0x7f36b888,
    COLOR_Format24bitBGR888 = 0x0000000c,
    COLOR_Format32bitABGR8888 = 0x7f00a000,
    COLOR_FormatRGBAFlexible = 0x7f36a888,
    COLOR_Format16bitRGB565 = 0x00000006,
    COLOR_FormatSurface = 0x7f000789,
} ;

enum FFHlMediaCodecPcmFormat {
    HLMEDIACODEC_PCM_16BIT = 2,
    HLMEDIACODEC_PCM_8BIT = 3,
    HLMEDIACODEC_PCM_FLOAT = 4,
};

enum FFHlMediaCodecPcmFormat ff_hlmediacodec_get_pcm_format(enum AVSampleFormat sample_fmt);
enum AVSampleFormat ff_hlmediacodec_get_sample_fmt(enum FFHlMediaCodecPcmFormat pcm_fmt);
enum FFHlMediaCodecColorFormat ff_hlmediacodec_get_color_format(enum AVPixelFormat pix_fmt);
enum AVPixelFormat ff_hlmediacodec_get_pix_fmt(enum FFHlMediaCodecColorFormat color_fmt);
const char* ff_hlmediacodec_get_mime(enum AVCodecID codec_id);

void hi_loge(void* ctx, const char* fmt, ...);
void hi_logw(void* ctx, const char* fmt, ...);
void hi_logi(void* ctx, const char* fmt, ...);
void hi_logd(void* ctx, const char* fmt, ...);
void hi_logt(void* ctx, const char* fmt, ...);

#endif
