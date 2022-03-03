/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_FFMPEGOUTPUT_HPP
#define SCREENCAPTURE_FFMPEGOUTPUT_HPP

#include "common.hpp"
#include "libavcommon.hpp"
#include "VAAPIEncoder.hpp"
#include "VAAPIScaler.hpp"
#include "Muxer.hpp"
#include <memory>
#include <chrono>

namespace ffmpeg
{

class FFmpegOutput
{
	unsigned int width;
	unsigned int height;
	PixelFormat format;
	AVBufferRef *drmDevice;
	AVBufferRef *vaapiDevice;
	std::chrono::time_point<std::chrono::steady_clock> startTime;
	std::unique_ptr<VAAPIEncoder> encoder;
	std::unique_ptr<VAAPIScaler> scaler;
	std::unique_ptr<Muxer> muxer;

public:
	FFmpegOutput(Rect sourceDimensions, PixelFormat sourcePixelFormat, bool withDRMPrime);

	~FFmpegOutput() noexcept;

	void pushFrame(AVFrame_Heap frame);
};

}

#endif //SCREENCAPTURE_FFMPEGOUTPUT_HPP
