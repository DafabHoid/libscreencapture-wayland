/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "common.hpp"
#include "libavcommon.hpp"
#include "VAAPIEncoder.hpp"
#include "VAAPIScaler.hpp"
#include "Muxer.hpp"
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

struct LibAVContext
{
	int width;
	int height;
	PixelFormat format;
	AVBufferRef* drmDevice;
	AVBufferRef* vaapiDevice;
	time_point<steady_clock> startTime;
	std::unique_ptr<VAAPIEncoder> encoder;
	std::unique_ptr<VAAPIScaler> scaler;
	std::unique_ptr<Muxer> muxer;
} libAV;

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

static void pushFrame(AVFrame_Heap frame)
{
	// assign a monotonically increasing PTS timestamp
	// the timebase we use is microseconds, and PTS = 0 means the start of the stream
	auto now = steady_clock::now();
	auto fromStart = now - libAV.startTime;
	frame->pts = duration_cast<microseconds>(fromStart).count();

	libAV.scaler->enqueueFrame(std::move(frame));
}

AVFrame* wrapInAVFrame(const MemoryFrame& frame, FrameDoneCallback cb) noexcept
{
	auto f = av_frame_alloc();
	f->width = libAV.width;
	f->height = libAV.height;
	f->format = pixelFormat2AV(libAV.format);
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

void pushFrame(MemoryFrame frame, FrameDoneCallback cb)
{
	pushFrame(AVFrame_Heap(wrapInAVFrame(frame, std::move(cb))));
}

void pushFrame(DmaBufFrame frame, FrameDoneCallback cb)
{
	pushFrame(AVFrame_Heap(wrapInAVFrame(frame, std::move(cb))));
}

void initLAV(int sourceWidth, int sourceHeight, PixelFormat sourcePixelFormat, bool withDRMPrime)
{
	libAV.width = sourceWidth;
	libAV.height = sourceHeight;
	libAV.format = sourcePixelFormat;

#ifndef NDEBUG
	av_log_set_level(AV_LOG_VERBOSE);
#endif

	int r;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif

	r = av_hwdevice_ctx_create(&libAV.drmDevice, AV_HWDEVICE_TYPE_DRM, "/dev/dri/renderD129", nullptr, 0);
	if (r)
		throw LibAVException(r, "Opening the DRM node failed");

	r = av_hwdevice_ctx_create_derived(&libAV.vaapiDevice, AV_HWDEVICE_TYPE_VAAPI, libAV.drmDevice, 0);
	if (r < 0)
		throw LibAVException(r, "Creating a VAAPI device from DRM node failed");


	constexpr unsigned int targetWidth = 1920, targetHeight = 1080;
	libAV.encoder = std::make_unique<VAAPIEncoder>(targetWidth, targetHeight, libAV.vaapiDevice, [](AVPacket& p)
	{
		libAV.muxer->writePacket(p);
	});

	libAV.muxer = std::make_unique<Muxer>("rtsp://[::1]:8654/screen", "rtsp", libAV.encoder->getCodecContext());

	libAV.scaler = std::make_unique<VAAPIScaler>(Rect {static_cast<unsigned int>(sourceWidth), static_cast<unsigned int>(sourceHeight)},
	        pixelFormat2AV(libAV.format), Rect {targetWidth, targetHeight},
	        libAV.drmDevice, libAV.vaapiDevice, withDRMPrime,
	        [] (AVFrame_Heap f)
	        {
	            libAV.encoder->enqueueFrame(std::move(f));
	        });

	libAV.startTime = steady_clock::now();
}

void deinitLibAV() noexcept
{
	libAV.scaler.reset();
	libAV.encoder.reset();
	libAV.muxer.reset();
	av_buffer_unref(&libAV.vaapiDevice);
	av_buffer_unref(&libAV.drmDevice);
}