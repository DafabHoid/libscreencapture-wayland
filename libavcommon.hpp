/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_LIBAVCOMMON_HPP
#define SCREENCAPTURE_LIBAVCOMMON_HPP

#include "common.hpp"
#include <exception>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
}

namespace ffmpeg
{

class LibAVException : public std::exception
{
	char message[128];
public:
	LibAVException(int errorCode, const char* format, ...) __attribute__((format(printf, 3, 4)));
	const char* what() const noexcept override { return message; }
};

static constexpr inline AVPixelFormat pixelFormat2AV(PixelFormat format)
{
	switch (format)
	{
	case PixelFormat::BGRA:
		return AV_PIX_FMT_BGRA;
	case PixelFormat::RGBA:
		return AV_PIX_FMT_RGBA;
	case PixelFormat::BGRX:
		return AV_PIX_FMT_BGR0;
	case PixelFormat::RGBX:
		return AV_PIX_FMT_RGB0;
	}
}

AVFrame* wrapInAVFrame(std::unique_ptr<MemoryFrame> frame) noexcept;
AVFrame* wrapInAVFrame(std::unique_ptr<DmaBufFrame> frame) noexcept;

struct AVFrameFree
{
	void operator()(AVFrame* f)
	{
		av_frame_free(&f);
	}
};
using AVFrame_Heap = std::unique_ptr<AVFrame, AVFrameFree>;

}

#endif //SCREENCAPTURE_LIBAVCOMMON_HPP
