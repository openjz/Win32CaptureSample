/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

 /**
  * @file libavformat muxing API usage example
  * @example mux.c
  *
  * Generate a synthetic audio and video signal and mux them to a media file in
  * any supported libavformat format. The default codecs are used.
  */

#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "common.h"

extern "C"
{
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/timestamp.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/hwcontext.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/bprint.h"
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "ffmpeg_utils.h"
#include "ffmpeg_encode.h"

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream* st;
    AVCodecContext* enc;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame* frame;
    AVFrame* tmp_frame;

    AVPacket* tmp_pkt;

    float t, tincr, tincr2;

    struct SwsContext* sws_ctx;
    struct SwrContext* swr_ctx;
} OutputStream;

#define STREAM_DURATION   300.0
int g_framerate = 30; /* 30 images/s */
AVPixelFormat STREAM_PIX_FMT = AV_PIX_FMT_NV12;
//#define STREAM_PIX_FMT    AV_PIX_FMT_RGBA
AVPixelFormat g_source_pix_fmt = AV_PIX_FMT_BGRA;
#define SCALE_FLAGS SWS_BICUBIC

OutputStream video_st = { 0 }, audio_st = { 0 };
const AVOutputFormat* fmt;
const char* filename;
const char* pngfilepath;
AVFormatContext* oc;
const AVCodec* audio_codec = NULL, * video_codec = NULL;
int have_video = 0, have_audio = 0;
int encode_video = 0, encode_audio = 0;

//const char* g_encoder = "h264_qsv";
//AVHWDeviceType g_hwType = AVHWDeviceType::AV_HWDEVICE_TYPE_QSV;
//AVPixelFormat g_hwPixfmt = AVPixelFormat::AV_PIX_FMT_QSV;

const char* g_encoder = "h264_nvenc";
AVHWDeviceType g_hwType = AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA;
AVPixelFormat g_hwPixfmt = AVPixelFormat::AV_PIX_FMT_CUDA;

AVFrame* g_source_frame = nullptr;
AVFrame* g_encode_frame = nullptr;
AVBufferRef* g_hw_device_ctx = nullptr;
AVBufferRef* g_hw_frames_ctx = nullptr;
AVFrame* g_hw_frame = nullptr;

SwsContext* g_sws_ctx = nullptr;  //缩放+像素格式转换
int g_width = 1920;
int g_height = 1080;

int g_src_width = 2560;
int g_src_height = 1600;

AVRational g_displayRational{ 1,1 };

AVDictionary* g_codec_opts = NULL;

int get_frame_from_png_file(const char* filename, AVFrame* frame);
int copy_frame(AVFrame* dist_frame, AVFrame* src_frame);
AVFrame* alloc_frame(enum AVPixelFormat pix_fmt, int width, int height);
int save_frame_as_png(AVFrame* frame, const char* filename);

static void log_packet(const AVFormatContext* fmt_ctx, const AVPacket* pkt)
{
    AVRational* time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    PLOG(plog::debug)
        << " pts:" << av_ts_make_string(FFmpegAVTSTimeStrType{ 0 }, pkt->pts)
        << " pts_time:" << av_ts_make_time_string(FFmpegAVTSTimeStrType{ 0 }, pkt->pts, time_base)
        << " dts:" << av_ts_make_string(FFmpegAVTSTimeStrType{ 0 }, pkt->dts)
        << " dts_time:" << av_ts_make_time_string(FFmpegAVTSTimeStrType{ 0 }, pkt->dts, time_base)
        << " duration:" << av_ts_make_string(FFmpegAVTSTimeStrType{ 0 }, pkt->duration)
        << " duration_time:" << av_ts_make_time_string(FFmpegAVTSTimeStrType{ 0 }, pkt->duration, time_base)
        << " stream_index:" << pkt->stream_index;
}

static int write_frame(AVFormatContext* fmt_ctx, AVCodecContext* c,
    AVStream* st, AVFrame* frame, AVPacket* pkt)
{
    int ret;

    // send the frame to the encoder
    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        PLOG(plog::debug) << "Error sending a frame to the encoder: %s\n"
            << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
        return 1;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            PLOG(plog::debug) << "Error encoding a frame: %s\n"
                << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
            return 1;
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(pkt, c->time_base, st->time_base);
        pkt->stream_index = st->index;

        /* Write the compressed frame to the media file. */
        log_packet(fmt_ctx, pkt);
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            PLOG(plog::debug) << "Error while writing output packet: %s\n"
                << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
            return 1;
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

/* Add an output stream. */
static void add_stream(OutputStream* ost, AVFormatContext* oc,
    const AVCodec** codec,
    enum AVCodecID codec_id, const char* codec_name)
{
    int ret = 0;
    AVCodecContext* c;
    int i;
    const AVCodecHWConfig* hw_config = NULL;
    AVHWFramesContext* g_frames_ctx = nullptr;
    AVHWFramesContext* frames_ctx_in = nullptr;
    AVHWFramesContext* scale_frames_ctx_dst = nullptr;
    

    /* find the encoder */
    if (codec_name != nullptr)
    {
        *codec = avcodec_find_encoder_by_name(codec_name);
    }
    else
    {
        *codec = avcodec_find_encoder(codec_id);
    }
    if (!(*codec)) {
        PLOG(plog::debug) << "Could not find encoder for '%s'\n"
            << avcodec_get_name(codec_id);
        return;
    }

    if ((*codec)->type == AVMEDIA_TYPE_VIDEO)
    {
        int ret = av_hwdevice_ctx_create(&g_hw_device_ctx, g_hwType, NULL, NULL, 0);
        if (ret < 0) {
            PLOG(plog::debug) << "Failed to create CUDA device: %s\n"
                << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
            return;
        }
        // 检查编码器支持的硬件配置
        for (int i = 0;; i++) {
            hw_config = avcodec_get_hw_config(*codec, i);
            if (!hw_config) {
                PLOG(plog::debug) << "Encoder %s does not support hardware acceleration\n" << (*codec)->name;
                return;
            }
            if (hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                hw_config->device_type == g_hwType) {
                break;
            }
        }
    }

    ost->tmp_pkt = av_packet_alloc();
    if (!ost->tmp_pkt) {
        PLOG(plog::debug) << "Could not allocate AVPacket\n";
        return;
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        PLOG(plog::debug) << "Could not allocate stream\n";
        return;
    }
    ost->st->id = oc->nb_streams - 1;
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        PLOG(plog::debug) << "Could not alloc an encoding context\n";
        return;
    }
    ost->enc = c;

    AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate = 64000;
        c->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
        }
        
        av_channel_layout_copy(&c->ch_layout, &ch_layout);
        ost->st->time_base = AVRational{ 1, c->sample_rate };
        break;

    case AVMEDIA_TYPE_VIDEO:
        // 绑定硬件设备
        c->hw_device_ctx = av_buffer_ref(g_hw_device_ctx); // 绑定硬件设备
        c->pix_fmt = g_hwPixfmt;                 // 使用硬件支持的像素格式（如 g_hwPixfmt）
        c->width = g_width;                                 // 视频宽度
        c->height = g_height;                                // 视频高度
        c->framerate = AVRational{ g_framerate, 1 };
        //c->bit_rate = 1500000;                           // 码率
        c->gop_size = 30; /* emit one intra frame every twelve frames at most */

        // 设置编码器参数（可选）
        //av_opt_set(c->priv_data, "preset", "slow", 0);    // 编码预设
        //av_opt_set(c->priv_data, "profile", "main", 0);   // 编码配置文件

        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = AVRational{ 1, g_framerate };
        //ost->st->time_base = AVRational{ 1, 1000 };

        c->time_base = ost->st->time_base;
        ost->st->sample_aspect_ratio = AVRational{ g_displayRational.num *g_height, g_displayRational.den *g_width };
        c->sample_aspect_ratio = ost->st->sample_aspect_ratio;
        
        //flags 是编码器的公有属性
        av_dict_set(&g_codec_opts, "profile", "high", AV_DICT_MATCH_CASE);
        if (std::string(g_encoder) == std::string("h264_nvenc"))
        {
            av_dict_set_int(&g_codec_opts, "cq", 23, AV_DICT_MATCH_CASE);
        }
        if (std::string(g_encoder) == std::string("h264_qsv"))
        {
            //av_dict_set(&g_codec_opts, "preset", "slow", AV_DICT_MATCH_CASE);
            av_dict_set_int(&g_codec_opts, "global_quality", 23, AV_DICT_MATCH_CASE);
            av_dict_set_int(&g_codec_opts, "bf", 0, AV_DICT_MATCH_CASE);
        }

        // 创建硬件帧池
        g_hw_frames_ctx = av_hwframe_ctx_alloc(g_hw_device_ctx);
        if (!g_hw_frames_ctx) {
            PLOG(plog::debug) << "Failed to allocate HW frames context\n";
            return;
        }

        g_frames_ctx = (AVHWFramesContext*)g_hw_frames_ctx->data;
        g_frames_ctx->format = g_hwPixfmt;  // 与编码器像素格式一致
        g_frames_ctx->sw_format = STREAM_PIX_FMT;  // 输入数据的软件格式（需转换为此格式）
        g_frames_ctx->width = c->width;
        g_frames_ctx->height = c->height;
        g_frames_ctx->initial_pool_size = 20;        // 预分配帧池大小

        // 初始化硬件帧池
        ret = av_hwframe_ctx_init(g_hw_frames_ctx);
        if (ret < 0) {
            PLOG(plog::debug) << "Failed to initialize HW frames context: %s\n" 
                << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
            return ;
        }

        // 将硬件帧池绑定到编码器上下文
        c->hw_frames_ctx = av_buffer_ref(g_hw_frames_ctx);

        //分配一个编码帧
        g_hw_frame = av_frame_alloc();
        g_hw_frame->format = g_hwPixfmt;          // 必须与硬件帧池格式一致
        g_hw_frame->width = c->width;
        g_hw_frame->height = c->height;

        ret = av_hwframe_get_buffer(g_hw_frames_ctx, g_hw_frame, 0);
        if (ret < 0) {
            PLOG(plog::debug) << "Failed to allocate HW frame: %s\n"
                << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
            return;
        }

        g_sws_ctx = sws_getContext(
            g_src_width, g_src_height, g_source_pix_fmt,   // 输入：RGBA格式
            g_width, g_height, STREAM_PIX_FMT,   // 输出：NV12格式
            SWS_BILINEAR, nullptr, nullptr, nullptr    // 缩放算法和参数
        );

        g_source_frame = alloc_frame(g_source_pix_fmt, g_src_width, g_src_height);
        g_encode_frame = alloc_frame(STREAM_PIX_FMT, g_width, g_height);
        break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame* alloc_audio_frame(enum AVSampleFormat sample_fmt,
    const AVChannelLayout* channel_layout,
    int sample_rate, int nb_samples)
{
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        PLOG(plog::debug) << "Error allocating an audio frame\n";
        return nullptr;
    }

    frame->format = sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, channel_layout);
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        if (av_frame_get_buffer(frame, 0) < 0) {
            PLOG(plog::debug) << "Error allocating an audio buffer\n";
            return nullptr;
        }
    }

    return frame;
}

static void open_audio(AVFormatContext* oc, const AVCodec* codec,
    OutputStream* ost, AVDictionary* opt_arg)
{
    AVCodecContext* c;
    int nb_samples;
    int ret;
    AVDictionary* opt = NULL;

    c = ost->enc;

    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        PLOG(plog::debug) << "Could not open audio codec: %s\n"
            << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
        return;
    }

    /* init signal generator */
    ost->t = 0;
    ost->tincr = 2 * M_PI * 440.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    //ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;
    ost->tincr2 = 0;

    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;

    ost->frame = alloc_audio_frame(c->sample_fmt, &c->ch_layout,
        c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &c->ch_layout,
        c->sample_rate, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        PLOG(plog::debug) << "Could not copy the stream parameters\n";
        return;
    }

    /* create resampler context */
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        PLOG(plog::debug) << "Could not allocate resampler context\n";
        return;
    }

    /* set options */
    av_opt_set_chlayout(ost->swr_ctx, "in_chlayout", &c->ch_layout, 0);
    av_opt_set_int(ost->swr_ctx, "in_sample_rate", c->sample_rate, 0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_chlayout(ost->swr_ctx, "out_chlayout", &c->ch_layout, 0);
    av_opt_set_int(ost->swr_ctx, "out_sample_rate", c->sample_rate, 0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt", c->sample_fmt, 0);

    /* initialize the resampling context */
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        PLOG(plog::debug) << "Failed to initialize the resampling context\n";
        return;
    }
}

void make_audio_frame(OutputStream* ost)
{
    AVFrame* frame = ost->tmp_frame;
    int j, i, v;
    int16_t* q = (int16_t*)frame->data[0];

    for (j = 0; j < frame->nb_samples; j++) {
        v = (int)(sin(ost->t) * 10000);
        for (i = 0; i < ost->enc->ch_layout.nb_channels; i++)
            *q++ = v;
        ost->t += ost->tincr;
        ost->tincr += ost->tincr2;
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static AVFrame* get_audio_frame(OutputStream* ost)
{
    AVFrame* frame = ost->tmp_frame;
    int j, i, v;
    int16_t* q = (int16_t*)frame->data[0];

    /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, ost->enc->time_base,
        STREAM_DURATION, AVRational{ 1, 1 }) > 0)
        return NULL;

    for (j = 0; j < frame->nb_samples; j++) {
        v = (int)(sin(ost->t) * 10000);
        for (i = 0; i < ost->enc->ch_layout.nb_channels; i++)
            *q++ = v;
        ost->t += ost->tincr;
        ost->tincr += ost->tincr2;
    }

    frame->pts = ost->next_pts;
    ost->next_pts += frame->nb_samples;

    return frame;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_audio_frame(AVFormatContext* oc, OutputStream* ost)
{
    AVCodecContext* c;
    AVFrame* frame;
    int ret;
    int dst_nb_samples;

    c = ost->enc;

    frame = get_audio_frame(ost);

    if (frame) {
        /* convert samples from native format to destination codec format, using the resampler */
        /* compute destination number of samples */
        dst_nb_samples = swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples;
        av_assert0(dst_nb_samples == frame->nb_samples);

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            return 1;

        /* convert to destination format */
        ret = swr_convert(ost->swr_ctx,
            ost->frame->data, dst_nb_samples,
            (const uint8_t**)frame->data, frame->nb_samples);
        if (ret < 0) {
            PLOG(plog::debug) << "Error while converting\n";
            return 1;
        }
        frame = ost->frame;

        frame->pts = av_rescale_q(ost->samples_count, AVRational{ 1, c->sample_rate }, c->time_base);
        ost->samples_count += dst_nb_samples;
    }

    return write_frame(oc, c, ost->st, frame, ost->tmp_pkt);
}

/**************************************************************/
/* video output */

AVFrame* alloc_frame(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame* frame;
    int ret;

    frame = av_frame_alloc();
    if (!frame)
        return NULL;

    frame->format = pix_fmt;
    frame->width = width;
    frame->height = height;
    //frame->sample_aspect_ratio = AVRational{ g_src_width, g_src_height };

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        PLOG(plog::debug) << "Could not allocate frame data.\n";
        return nullptr;
    }

    return frame;
}

static void open_video(AVFormatContext* oc, const AVCodec* codec,
    OutputStream* ost)
{
    int ret;
    AVCodecContext* c = ost->enc;
    AVDictionary* opt = NULL;

    av_dict_copy(&opt, g_codec_opts, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        PLOG(plog::debug) << "Could not open video codec: %s\n"
            << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
        return;
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_frame(STREAM_PIX_FMT, c->width, c->height);
    if (!ost->frame) {
        PLOG(plog::debug) << "Could not allocate video frame\n";
        return;
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != STREAM_PIX_FMT) {
        ost->tmp_frame = alloc_frame(STREAM_PIX_FMT, c->width, c->height);
        if (!ost->tmp_frame) {
            PLOG(plog::debug) << "Could not allocate temporary video frame\n";
            return;
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        PLOG(plog::debug) << "Could not copy the stream parameters\n";
        return;
    }
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame* pict, int frame_index,
    int width, int height)
{
    int x, y, i;

    i = frame_index;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

static void fill_nv12_image(AVFrame* pict, int frame_index, int width, int height) {
    int x, y, i;
    i = frame_index;

    /* Y 平面（与 YUV420P 相同） */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
        }
    }

    /* UV 平面（NV12 中 UV 交错存储） */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            // 计算 UV 分量的位置：每个 UV 对占用 2 字节（U 在前，V 在后）
            int uv_index = y * pict->linesize[1] + 2 * x;

            // 生成 U 分量（Cb）
            pict->data[1][uv_index] = 128 + y + i * 2;  // U 值

            // 生成 V 分量（Cr）
            pict->data[1][uv_index + 1] = 64 + x + i * 5;   // V 值
        }
    }
}

static void fill_rgba_image(AVFrame* pict, int frame_index,
    int width, int height)
{
    int x, y;
    uint8_t* rgba;

    for (y = 0; y < height; y++) {
        rgba = pict->data[0] + y * pict->linesize[0];
        for (x = 0; x < width; x++) {
            // 生成RGB分量
            uint8_t r = (x + frame_index) % 256;
            uint8_t g = (y + frame_index * 2) % 256;
            uint8_t b = (x + y + frame_index * 3) % 256;

            // 优化后的Alpha分量（全范围0-255）
            //uint8_t a = ((uint32_t)x * y + frame_index * 7) % 256;
            uint8_t a = 255;

            // 写入RGBA
            rgba[0] = r;
            rgba[1] = g;
            rgba[2] = b;
            rgba[3] = a;

            rgba += 4;
        }

        // 可选：填充行对齐区域
        // memset(rgba, 0, pict->linesize[0] - width * 4);
    }
}

static AVFrame* get_video_frame(OutputStream* ost, AVFrame * source_frame, uint64_t frame_interval)
{
    if (source_frame == nullptr)
    {
        return nullptr;
    }
    AVCodecContext* c = ost->enc;

    AVFrame* final_frame = source_frame;
    if (STREAM_PIX_FMT != g_source_pix_fmt)
    {
        //转换像素格式
        sws_scale(
            g_sws_ctx,
            source_frame->data, source_frame->linesize, 0, source_frame->height,  // 输入数据和参数
            g_encode_frame->data, g_encode_frame->linesize  // 输出数据和参数
        );
        final_frame = g_encode_frame;
    }

    int ret = av_hwframe_transfer_data(g_hw_frame, final_frame, 0);
    if (ret < 0) {
        PLOG(plog::debug) << "Error transferring data to HW frame: %s\n"
            << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
        return nullptr;
    }

    g_hw_frame->pts = ost->next_pts;
    //ost->next_pts += frame_interval;
    ost->next_pts += 1;
    return g_hw_frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext* oc, OutputStream* ost, AVFrame *frame, uint64_t frame_interval)
{
    AVFrame* encoded_frame = get_video_frame(ost, frame, frame_interval);
    return write_frame(oc, ost->enc, ost->st, encoded_frame, ost->tmp_pkt);
}

static void close_stream(AVFormatContext* oc, OutputStream* ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    av_packet_free(&ost->tmp_pkt);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

int get_frame_from_png_file(const char* filename, AVFrame* frame) {
    int ret = 0;

    AVFormatContext* format_ctx = nullptr;
    AVDictionary* format_opts = nullptr;
    AVCodecContext* av_codec_ctx = nullptr;
    AVDictionary* codec_opts = nullptr;
    const AVCodec* codec = nullptr;
    AVPacket* pkt = nullptr;
    do
    {
        av_dict_set(&format_opts, "probesize", "5000000", 0);
        ret = avformat_open_input(&format_ctx, filename, NULL, &format_opts);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error: avformat_open_input failed\n");
            break;
        }
        ret = avformat_find_stream_info(format_ctx, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
            break;
        }

        av_codec_ctx = avcodec_alloc_context3(NULL);
        if (!av_codec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n", 0);
            ret = AVERROR(ENOMEM);
            break;
        }
        ret = avcodec_parameters_to_context(av_codec_ctx, format_ctx->streams[0]->codecpar);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy decoder parameters to input decoder context "
                "for stream #%u\n", 0);
            break;
        }

        av_dict_set(&codec_opts, "sub_text_format", "ass", 0);
        codec = avcodec_find_decoder(av_codec_ctx->codec_id);
        if (!codec) {
            av_log(NULL, AV_LOG_ERROR, "codec not support\n");
            ret = AVERROR_DECODER_NOT_FOUND;
            break;
        }
        if ((ret = avcodec_open2(av_codec_ctx, codec, NULL)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", 0);
            break;
        }

        pkt = av_packet_alloc();
        if (!pkt)
        {
            av_log(NULL, AV_LOG_ERROR, "Alloc packet failed\n");
            ret = AVERROR(ENOMEM);
            break;
        }
        ret = av_read_frame(format_ctx, pkt);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_read_frame failed\n");
            break;
        }
        ret = avcodec_send_packet(av_codec_ctx, pkt);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
            break;
        }
        ret = avcodec_receive_frame(av_codec_ctx, frame);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame failed\n");
            break;
        }
    } while (false);

    avformat_close_input(&format_ctx);
    av_dict_free(&format_opts);
    avcodec_free_context(&av_codec_ctx);
    av_dict_free(&codec_opts);
    av_packet_free(&pkt);
    return ret;
}

int copy_frame(AVFrame* dist_frame, AVFrame* src_frame)
{
    
    dist_frame->width = src_frame->width;
    dist_frame->height = src_frame->height;
    dist_frame->format = src_frame->format;

    av_frame_copy(dist_frame, src_frame);
    av_frame_copy_props(dist_frame, src_frame);
    return 0;
}

//export
int start_encode(const char * in_filename, AVPixelFormat format, int width, int height, int framerate, AVRational displayRational)
{
    int ret = 0;
    filename = in_filename;
    g_source_pix_fmt = format;
    g_framerate = framerate;
    g_src_width = width;
    g_src_height = height;
    g_displayRational = displayRational;
    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc)
    {
        return 1;
    }
    fmt = oc->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, oc, &video_codec, fmt->video_codec, g_encoder);
        have_video = 1;
        encode_video = 1;
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video)
        open_video(oc, video_codec, &video_st);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            PLOG(plog::debug) << "Could not open '%s': %s\n" << filename
                << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
            return 1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, nullptr);
    if (ret < 0) {
        PLOG(plog::debug) << "Error occurred when opening output file: %s\n"
            << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
        return 1;
    }
}

//frame_interval 参数表示当前帧和上一帧之间的间隔，目前没用到这个参数
int send_video_frame(AVFrame* frame, uint64_t frame_interval)
{
    //uint64_t cur_time_ms = GetCurrentTimestampMicro();
    //uint64_t old_time_ms = GetCurrentTimestampMicro();
    int ret = write_video_frame(oc, &video_st, frame, frame_interval);
    return ret;
}

int stop_encode()
{
    write_video_frame(oc, &video_st, nullptr, 0);

    av_write_trailer(oc);

    /* Close each codec. */
    if (have_video)
        close_stream(oc, &video_st);
    if (have_audio)
        close_stream(oc, &audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&oc->pb);

    /* free the stream */
    avformat_free_context(oc);
    av_frame_free(&g_encode_frame);
    av_frame_free(&g_source_frame);
    g_source_frame = nullptr;

    av_buffer_unref(&g_hw_device_ctx);
    av_buffer_unref(&g_hw_frames_ctx);
    av_frame_free(&g_hw_frame);
    sws_freeContext(g_sws_ctx);
    av_dict_free(&g_codec_opts);
    return 0;
}

AVFrame* ref_encoder_input_frame()
{
    if (g_source_frame == nullptr || av_frame_make_writable(g_source_frame) < 0)
    {
        return nullptr;
    }
    return g_source_frame;
}

int save_frame_as_png(AVFrame* frame, const char* filename)
{
    int ret = 0;
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!encoder)
    {
        return -1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(encoder);
    if (!codec_ctx)
    {
        return -2;
    }

    // 配置编码参数
    codec_ctx->width = frame->width;
    codec_ctx->height = frame->height;
    codec_ctx->pix_fmt = g_source_pix_fmt;  // PNG常用格式
    codec_ctx->time_base = { 1, 25 };         // 对静态图片无实质影响

    if ((ret = avcodec_open2(codec_ctx, encoder, nullptr)) < 0)
    {
        PLOG(plog::debug) << "save_frame_as_png, avcodec_open2 failed, err msg: " << av_make_error_string(FFmpegErrStrType{ 0 }, sizeof(FFmpegErrStrType), ret);
        avcodec_free_context(&codec_ctx);
        return -3;
    }

    AVFrame* converted_frame = av_frame_alloc();
    // 像素格式转换（如果需要）
    SwsContext* sws_ctx = nullptr;
    if (frame->format != codec_ctx->pix_fmt) {
        sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
            codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr);


        converted_frame->format = codec_ctx->pix_fmt;
        converted_frame->width = codec_ctx->width;
        converted_frame->height = codec_ctx->height;
        av_frame_get_buffer(converted_frame, 0);

        sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
            converted_frame->data, converted_frame->linesize);

        frame = converted_frame;  // 使用转换后的帧
    }

    // 编码帧
    ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0) {
        if (sws_ctx) sws_freeContext(sws_ctx);
        avcodec_free_context(&codec_ctx);
        return -4;
    }

    AVPacket* pkt = av_packet_alloc();
    FILE* output_file = fopen(filename, "wb");
    if (!output_file) return -5;

    // 接收编码数据包
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;

        if (ret < 0) {
            fclose(output_file);
            av_packet_unref(pkt);
            break;
        }

        // 写入文件
        fwrite(pkt->data, 1, pkt->size, output_file);
        av_packet_unref(pkt);
    }

    // 清理资源
    av_frame_free(&converted_frame);
    if (sws_ctx) sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    av_packet_free(&pkt);
    fclose(output_file);
    av_dict_free(&g_codec_opts);
    return 0;
}