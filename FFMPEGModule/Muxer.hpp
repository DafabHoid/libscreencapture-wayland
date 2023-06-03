/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_MUXER_HPP
#define SCREENCAPTURE_MUXER_HPP

#include <string>
#include "../common.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace ffmpeg
{

class Muxer
{
	AVStream* outputVideoStream;
	AVFormatContext* formatContext;
	AVRational codecTimeBase;

public:
	SCW_EXPORT Muxer(const std::string& outputURL, const std::string& containerFormat, const AVCodecContext* videoCodecCtx);
	SCW_EXPORT ~Muxer() noexcept;

	SCW_EXPORT void writePacket(AVPacket& p);

	SCW_EXPORT bool requiresStrictMonotonicTimestamps() const noexcept;
};

}


#endif //SCREENCAPTURE_MUXER_HPP
