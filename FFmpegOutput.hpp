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
#include <string>
#include <memory>

namespace ffmpeg
{

class FFmpegOutput
{
	std::unique_ptr<Muxer> muxer;
	std::unique_ptr<ThreadedVAAPIEncoder> encoder;
	std::unique_ptr<ThreadedVAAPIScaler> scaler;

	FFmpegOutput(
			std::unique_ptr<ThreadedVAAPIScaler> scaler,
	        std::unique_ptr<ThreadedVAAPIEncoder> encoder,
	        std::unique_ptr<Muxer> muxer) noexcept;

public:
	SCW_EXPORT void pushFrame(AVFrame_Heap frame);


	class Builder
	{
		Rect sourceSize;
		PixelFormat sourceFormat;
		bool isSourceDrmPrime;
		Rect targetSize;
		AVDictionary* codecOptions;
		std::string outputFormat;
		std::string outputPath;
		std::string hwDevicePath;

	public:
		SCW_EXPORT Builder(Rect sourceSize, PixelFormat sourceFormat, bool isDrmPrime) noexcept;

		SCW_EXPORT Builder& withHWDevice(std::string devicePath) noexcept
		{
			hwDevicePath = std::move(devicePath);
			return *this;
		}

		SCW_EXPORT Builder& withScaling(Rect scaledSize) noexcept
		{
			targetSize = scaledSize;
			return *this;
		}

		SCW_EXPORT Builder& withCodecOptions(AVDictionary* options) noexcept
		{
			codecOptions = options;
			return *this;
		}

		SCW_EXPORT Builder& withOutputFormat(std::string format) noexcept
		{
			outputFormat = std::move(format);
			return *this;
		}

		SCW_EXPORT Builder& withOutputPath(std::string path) noexcept
		{
			outputPath = std::move(path);
			return *this;
		}

		SCW_EXPORT FFmpegOutput build();
	};
};

}

#endif //SCREENCAPTURE_FFMPEGOUTPUT_HPP
