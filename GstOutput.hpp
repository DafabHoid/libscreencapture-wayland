/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_GSTOUTPUT_HPP
#define SCREENCAPTURE_GSTOUTPUT_HPP

#include "common.hpp"
#include <gst/gst.h>
#include <exception>
#include <string>

namespace gstreamer
{
using common::PixelFormat;
using common::Rect;

enum class Codec
{
	H264,
	H265,
};

class GstOutput
{
	GstElement* pipeline;
	GstElement* appSource;

	void pushFrame(GstBuffer* buf);
	GstOutput(Rect sourceSize, PixelFormat sourceFormat, Rect scaledSize,
	          const std::string& hwDevicePath, Codec codec,
	          const std::string& outputPath, const std::string& outputFormat);

public:
	SCW_EXPORT ~GstOutput() noexcept;
	SCW_EXPORT GstOutput(GstOutput&&);
	SCW_EXPORT GstOutput operator=(GstOutput&&);

	SCW_EXPORT void pushFrame(std::unique_ptr<common::MemoryFrame> frame);

	class Builder
	{
		Rect sourceSize;
		PixelFormat sourceFormat;
		Rect targetSize;
		std::string outputFormat;
		std::string outputPath;
		Codec codec;
		std::string hwDevicePath;

	public:
		SCW_EXPORT Builder(Rect sourceSize, PixelFormat sourceFormat) noexcept;

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

		SCW_EXPORT Builder& withCodec(Codec c) noexcept
		{
			codec = c;
			return *this;
		}

		SCW_EXPORT GstOutput build();
	};
};

class GStreamerException : public std::exception
{
	char message[128];
public:
	explicit GStreamerException(const char* messageFmtStr, ...) noexcept __attribute__((format(printf, 2, 3)));
	const char* what() const noexcept override { return message; }
};

}

#endif //SCREENCAPTURE_GSTOUTPUT_HPP
