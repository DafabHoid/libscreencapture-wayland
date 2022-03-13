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

namespace ffmpeg
{

class FFmpegOutput
{
	AVBufferRef *drmDevice;
	AVBufferRef *vaapiDevice;
	std::unique_ptr<VAAPIEncoder> encoder;
	std::unique_ptr<VAAPIScaler> scaler;
	std::unique_ptr<Muxer> muxer;

public:
	SCW_EXPORT FFmpegOutput(Rect sourceDimensions, PixelFormat sourcePixelFormat, bool withDRMPrime);

	SCW_EXPORT ~FFmpegOutput() noexcept;

	SCW_EXPORT void pushFrame(AVFrame_Heap frame);
};

}

#endif //SCREENCAPTURE_FFMPEGOUTPUT_HPP
