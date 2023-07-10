/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_FFMPEGOUTPUT_HPP
#define SCREENCAPTURE_FFMPEGOUTPUT_HPP

#include "../common.hpp"
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
	int64_t lastPts;

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
		Codec codec;
		std::string outputFormat;
		std::string outputPath;
		std::string hwDevicePath;

	public:
		/** Create a builder first to create a FFmpegOutput object in a stepwise fashion.
		 * Obligatory parameters must be provided in the constructor, while optional ones can be set using the {@code with*}
		 * methods. Finish the process with a call to build().
		 * @param sourceSize Dimensions of incoming frames in pixels
		 * @param sourceFormat Pixel format of incoming frames
		 * @param isDrmPrime True when incoming frames come as DRM PRIME file descriptors (DmaBuf shared) instead of regular memory,
		 *                   like those from wrapInAVFrame(std::unique_ptr<DmaBufFrame> frame)
		 */
		SCW_EXPORT Builder(Rect sourceSize, PixelFormat sourceFormat, bool isDrmPrime) noexcept;

		SCW_EXPORT ~Builder() noexcept;

		/** Encode on the device at this path.
		 * The default is {@code /dev/dri/renderD128}.
		 * Currently only a DRM render node path is supported, like {@code /dev/dri/renderD128}. It must support encoding
		 * via VAAPi. */
		SCW_EXPORT Builder& withHWDevice(std::string devicePath) noexcept
		{
			hwDevicePath = std::move(devicePath);
			return *this;
		}

		/** Scale frames to the given size before encoding
		 * @param scaledSize The rectangle which specifies the width and height the scaled frames should have. Must each be larger than zero. */
		SCW_EXPORT Builder& withScaling(Rect scaledSize) noexcept
		{
			targetSize = scaledSize;
			return *this;
		}

		/** Encode with this codec.
		 * By default, H.264 is used.
		 * Support depends on the hardware capabilities of your GPU and the ffmpeg version in use. */
		SCW_EXPORT Builder& withCodec(Codec c) noexcept
		{
			codec = c;
			return *this;
		}

		/** If you want to change any encoding parameters from their defaults, you can give a dictionary with options
		 * to the encoder.
		 * The available options depend on the codec and ffmpeg version, so see the ffmpeg documentation at {@a https://ffmpeg.org/ffmpeg-codecs.html}.
		 * @param options A dictionary with options. This builder will create a copy.
		 */
		SCW_EXPORT Builder& withCodecOptions(const AVDictionary* options) noexcept
		{
			av_dict_copy(&codecOptions, options, 0);
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
