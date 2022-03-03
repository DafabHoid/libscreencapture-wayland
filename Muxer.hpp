/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_MUXER_HPP
#define SCREENCAPTURE_MUXER_HPP

#include <string>
extern "C" {
#include <libavformat/avformat.h>
}

namespace ffmpeg
{

class Muxer
{
	AVStream* outputVideoStream;
	AVFormatContext* formatContext;
	AVRational codecTimeBase;

public:
	Muxer(const std::string& outputURL, const std::string& containerFormat, const AVCodecContext* videoCodecCtx);
	~Muxer() noexcept;

	void writePacket(AVPacket& p);
};

}


#endif //SCREENCAPTURE_MUXER_HPP
