#ifndef AVCODEC_HLMEDIACODEC_CODEC_H
#define AVCODEC_HLMEDIACODEC_CODEC_H

#include "hlmediacodec.h"

#define HLMEDIACODEC_BITRATE_MODE_CQ  0 //MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CQ
#define HLMEDIACODEC_BITRATE_MODE_VBR 1 //MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR
#define HLMEDIACODEC_BITRATE_MODE_CBR 2 //MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR

#define HLMEDIACODEC_MIN_TIMEOUT_USEC 1000//us
#define HLMEDIACODEC_MAX_TIMEOUT_USEC 10000000//us
#define HLMEDIACODEC_IN_SET_TIMEOUT_USEC 100000//us
#define HLMEDIACODEC_OU_SET_TIMEOUT_USEC 8000//us
#define HLMEDIACODEC_EOF_SET_TIMEOUT_USEC 30000//us

#define HLMEDIACODEC_MIN_TIMEOUT_TIMES 1
#define HLMEDIACODEC_MAX_TIMEOUT_TIMES 100
#define HLMEDIACODEC_IN_SET_TIMEOUT_TIMES 3
#define HLMEDIACODEC_DEC_OU_SET_TIMEOUT_TIMES 2
#define HLMEDIACODEC_ENC_OU_SET_TIMEOUT_TIMES 1


#define HLMEDIACODEC_CONFIGURE_FLAG_ENCODE 1
#define HLMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG 2
#define HLMEDIACODEC_BUFFER_FLAG_END_OF_STREAM 4
#define HLMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME 8

#define HLMINOK(v, l) ((v) >= (l) ? true : false)
#define HLMAXOK(v, l) ((v) <= (l) ? true : false)
#define HLOK(v, lv, rv) ((HLMAXOK(lv, rv) && HLMINOK(v, lv) && HLMAXOK(v, rv)) ? true : false)


typedef struct {
  int64_t   init_stamp;
  int64_t   uint_stamp;

  uint32_t  get_succ_cnt;
  uint32_t  get_fail_cnt;

  uint32_t  in_succ_cnt;
  uint32_t  in_fail_cnt;
  uint32_t  in_fail_again_cnt;

  uint32_t  ou_succ_cnt;
  uint32_t  ou_succ_frame_cnt;
  uint32_t  ou_succ_conf_cnt;
  uint32_t  ou_succ_idr_cnt;
  uint32_t  ou_succ_end_cnt;
  uint32_t  ou_fail_cnt;
  uint32_t  ou_fail_again_cnt;
  uint32_t  ou_fail_format_cnt;
  uint32_t  ou_fail_buffer_cnt;
  uint32_t  ou_fail_oth_cnt;
} HLMediaCodecStats;

typedef struct {
  AVClass* avclass;

  HLMediaCodecStats stats;
  bool inited;
  AMediaFormat*     mediaformat;
  AMediaCodec*      mediacodec;
  AVPacket          packet;
  uint8_t*          buffer;
  uint32_t          buffer_size;

  bool              in_eof;
  bool              ou_eof;
  int64_t           in_pts;
  int64_t           in_duration;

  int               in_timeout;
  int               ou_timeout;
  int               eof_timeout;
  int               in_timeout_times;
  int               ou_timeout_times;
} HLMediaCodecDecContext;

typedef struct {
    AVClass*          avclass;

    HLMediaCodecStats stats;
    bool              inited;
    AMediaFormat*     mediaformat;
    AMediaCodec*      mediacodec;
    AVFrame*          frame;

    bool              in_eof;
    bool              ou_eof;
    int64_t           in_pts;
    int64_t           in_duration;

    int               rc_mode;
    int               in_timeout;
    int               ou_timeout;
    int               eof_timeout;
    int               in_timeout_times;
    int               ou_timeout_times;
} HLMediaCodecEncContext;


int hlmediacodec_fill_format(AVCodecContext* avctx, AMediaFormat* mediaformat);
int hlmediacodec_fill_context(AMediaFormat* mediaformat, AVCodecContext* avctx);
int hlmediacodec_decode_buffer_to_frame(AVCodecContext* avctx, AMediaCodecBufferInfo bufferinfo, AVFrame* frame);
int hlmediacodec_encode_header(AVCodecContext* avctx);
int hlmediacodec_get_buffer_size(AVCodecContext* avctx);
void hlmediacodec_show_stats(AVCodecContext* avctx, HLMediaCodecStats stats);

#endif
