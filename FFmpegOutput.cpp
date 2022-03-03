/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "FFmpegOutput.hpp"
#include <cstdio>
#include <cstdarg>
#include <chrono>

using namespace std::chrono_literals;
using namespace std::chrono;

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
}

namespace ffmpeg
{

LibAVException::LibAVException(int errorCode, const char* messageFmtStr, ...)
{
	std::va_list v_args;
	va_start(v_args, messageFmtStr);
	char averrstr[AV_ERROR_MAX_STRING_SIZE];
	int n = std::snprintf(message, sizeof(message), "LibAV error %d (%s): ",
	                      errorCode, av_make_error_string(averrstr, sizeof(averrstr), errorCode));
	if (n > 0 && n < sizeof(message))
		std::vsnprintf(message + n, sizeof(message) - n, messageFmtStr, v_args);
	va_end(v_args);

	dumpStackTrace();
}

void FFmpegOutput::pushFrame(AVFrame_Heap frame)
{
	// assign a monotonically increasing PTS timestamp
	// the timebase we use is microseconds, and PTS = 0 means the start of the stream
	auto now = steady_clock::now();
	auto fromStart = now - startTime;
	frame->pts = duration_cast<microseconds>(fromStart).count();

	scaler->enqueueFrame(std::move(frame));
}

AVFrame* wrapInAVFrame(const MemoryFrame& frame, FrameDoneCallback cb) noexcept
{
	auto f = av_frame_alloc();
	f->width = frame.width;
	f->height = frame.height;
	f->format = pixelFormat2AV(frame.format);
	f->data[0] = static_cast<uint8_t*>(frame.memory) + frame.offset;
	f->linesize[0] = frame.stride;

	auto callbackOnHeap = new FrameDoneCallback(std::move(cb));
	auto frameDeleter = [](void* u, uint8_t*)
	{
		auto cb = static_cast<FrameDoneCallback*>(u);
		(*cb)();
		delete cb;
	};
	// create a dummy AVBuffer so reference counting works, but do not let it free the memory we don't own
	f->buf[0] = av_buffer_create(static_cast<uint8_t*>(frame.memory), frame.size, frameDeleter, callbackOnHeap, AV_BUFFER_FLAG_READONLY);
	return f;
}

AVFrame* wrapInAVFrame(const DmaBufFrame& frame, FrameDoneCallback cb) noexcept
{
	// construct an AVFrame that references a piece of DRM video memory

	// copy over the information about the DRM PRIME file descriptor and the frame properties
	auto* d = new AVDRMFrameDescriptor;
	d->nb_objects = 1;
	d->objects[0].fd = frame.drmObject.fd;
	d->objects[0].size = frame.drmObject.totalSize;
	d->objects[0].format_modifier = frame.drmObject.modifier;
	d->nb_layers = 1;
	d->layers[0].format = frame.drmFormat;
	d->layers[0].nb_planes = frame.planeCount;
	for (uint32_t i = 0; i < d->layers[0].nb_planes; ++i)
	{
		d->layers[0].planes[i].object_index = 0;
		d->layers[0].planes[i].offset = frame.planes[i].offset;
		d->layers[0].planes[i].pitch = frame.planes[i].pitch;
	}

	// the frame must have the format DRM_PRIME and contain a pointer to the AVDRMFrameDescriptor
	AVFrame* f = av_frame_alloc();
	f->format = AV_PIX_FMT_DRM_PRIME;
	f->data[0] = reinterpret_cast<uint8_t*>(d);
	f->width = frame.width;
	f->height = frame.height;

	// make sure reference counting works and the callback is called when the frame is deleted
	auto callbackOnHeap = new FrameDoneCallback(std::move(cb));
	auto frameDeleter = [](void* userData, uint8_t* bufData)
	{
		auto cb = static_cast<FrameDoneCallback*>(userData);
		(*cb)();
		delete cb;
		auto d = reinterpret_cast<AVDRMFrameDescriptor*>(bufData);
		delete d;
	};
	f->buf[0] = av_buffer_create(f->data[0], 0, frameDeleter, callbackOnHeap, AV_BUFFER_FLAG_READONLY);
	return f;
}

FFmpegOutput::FFmpegOutput(Rect sourceDimensions, PixelFormat sourcePixelFormat, bool withDRMPrime)
: width{sourceDimensions.w},
  height{sourceDimensions.h},
  format{sourcePixelFormat},
  drmDevice{},
  vaapiDevice{}
{
#ifndef NDEBUG
	av_log_set_level(AV_LOG_VERBOSE);
#endif

	int r;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif

	r = av_hwdevice_ctx_create(&drmDevice, AV_HWDEVICE_TYPE_DRM, "/dev/dri/renderD129", nullptr, 0);
	if (r)
		throw LibAVException(r, "Opening the DRM node failed");

	r = av_hwdevice_ctx_create_derived(&vaapiDevice, AV_HWDEVICE_TYPE_VAAPI, drmDevice, 0);
	if (r < 0)
		throw LibAVException(r, "Creating a VAAPI device from DRM node failed");


	constexpr unsigned int targetWidth = 1920, targetHeight = 1080;
	encoder = std::make_unique<VAAPIEncoder>(targetWidth, targetHeight, vaapiDevice, [this](AVPacket& p)
	{
		muxer->writePacket(p);
	});

	muxer = std::make_unique<Muxer>("rtsp://[::1]:8654/screen", "rtsp", encoder->getCodecContext());

	scaler = std::make_unique<VAAPIScaler>(Rect {width, height},
	        pixelFormat2AV(format), Rect {targetWidth, targetHeight},
	        drmDevice, vaapiDevice, withDRMPrime,
	        [this] (AVFrame_Heap f)
	        {
	            encoder->enqueueFrame(std::move(f));
	        });

	startTime = steady_clock::now();
}

FFmpegOutput::~FFmpegOutput() noexcept
{
	av_buffer_unref(&vaapiDevice);
	av_buffer_unref(&drmDevice);
	// scaler, encoder and muxer are implicitly stopped and destroyed here
}

}