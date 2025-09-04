#pragma once

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

int start_encode(const char* in_filename, AVPixelFormat format, int width, int height, int framerate, AVRational displayRational);

int send_video_frame(AVFrame* frame, uint64_t frame_interval);

int stop_encode();

AVFrame* ref_encoder_input_frame();

AVFrame* alloc_frame(enum AVPixelFormat pix_fmt, int width, int height);

int save_frame_as_png(AVFrame* frame, const char* filename);