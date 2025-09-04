#pragma once

#include <string>

extern "C"
{
#include "libavutil/error.h"
#include "libavutil/timestamp.h"
}

using FFmpegErrStrType = char[AV_ERROR_MAX_STRING_SIZE];
using FFmpegAVTSTimeStrType = char[AV_TS_MAX_STRING_SIZE];

int getFormatFromSampleFmt(const char** fmt, enum AVSampleFormat sample_fmt);
