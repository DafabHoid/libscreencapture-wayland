/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "FFmpegOutput.hpp"
#include <cstdio>
#include <cstdarg>
#include <chrono>

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
#ifndef NDEBUG
	dumpStackTrace();
#endif
}

FFmpegOutput::FFmpegOutput(std::unique_ptr<ThreadedVAAPIScaler> scaler,
                           std::unique_ptr<ThreadedVAAPIEncoder> encoder,
                           std::unique_ptr<Muxer> muxer) noexcept
: muxer(std::move(muxer)),
  encoder(std::move(encoder)),
  scaler(std::move(scaler))
{
	this->encoder->setFrameProcessedCallback([muxer = this->muxer.get()](AVPacket& p)
	{
		muxer->writePacket(p);
	});

	this->scaler->setFrameProcessedCallback([encoder = this->encoder.get()](AVFrame_Heap f)
	{
		encoder->processFrame(std::move(f));
	});
}

void FFmpegOutput::pushFrame(AVFrame_Heap frame)
{
	scaler->processFrame(std::move(frame));
}

AVFrame* wrapInAVFrame(std::unique_ptr<MemoryFrame> frame) noexcept
{
	auto f = av_frame_alloc();
	f->width = frame->width;
	f->height = frame->height;
	f->format = pixelFormat2AV(frame->format);
	f->data[0] = static_cast<uint8_t*>(frame->memory) + frame->offset;
	f->linesize[0] = frame->stride;
	f->pts = duration_cast<microseconds>(frame->pts).count();

	auto frameDeleter = [](void* u, uint8_t*)
	{
		auto f = static_cast<MemoryFrame*>(u);
		delete f;
	};
	// create a dummy AVBuffer so reference counting works, but do not let it free the memory we don't own
	f->buf[0] = av_buffer_create(static_cast<uint8_t*>(frame->memory), frame->size, frameDeleter, frame.release(), AV_BUFFER_FLAG_READONLY);
	return f;
}

AVFrame* wrapInAVFrame(std::unique_ptr<DmaBufFrame> frame) noexcept
{
	// construct an AVFrame that references a piece of DRM video memory

	// copy over the information about the DRM PRIME file descriptor and the frame properties
	auto* d = new AVDRMFrameDescriptor;
	d->nb_objects = 1;
	d->objects[0].fd = frame->drmObject.fd;
	d->objects[0].size = frame->drmObject.totalSize;
	d->objects[0].format_modifier = frame->drmObject.modifier;
	d->nb_layers = 1;
	d->layers[0].format = frame->drmFormat;
	d->layers[0].nb_planes = frame->planeCount;
	for (uint32_t i = 0; i < d->layers[0].nb_planes; ++i)
	{
		d->layers[0].planes[i].object_index = 0;
		d->layers[0].planes[i].offset = frame->planes[i].offset;
		d->layers[0].planes[i].pitch = frame->planes[i].pitch;
	}

	// the frame must have the format DRM_PRIME and contain a pointer to the AVDRMFrameDescriptor
	AVFrame* f = av_frame_alloc();
	f->format = AV_PIX_FMT_DRM_PRIME;
	f->data[0] = reinterpret_cast<uint8_t*>(d);
	f->width = frame->width;
	f->height = frame->height;
	f->pts = duration_cast<microseconds>(frame->pts).count();

	// make sure reference counting works and the frame is deleted when no longer needed
	auto frameDeleter = [](void* userData, uint8_t* bufData)
	{
		auto f = static_cast<DmaBufFrame*>(userData);
		delete f;
		auto d = reinterpret_cast<AVDRMFrameDescriptor*>(bufData);
		delete d;
	};
	f->buf[0] = av_buffer_create(f->data[0], 0, frameDeleter, frame.release(), AV_BUFFER_FLAG_READONLY);
	return f;
}

static void initFFmpeg() noexcept
{
#ifndef NDEBUG
	av_log_set_level(AV_LOG_VERBOSE);
#endif

	int r;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif

}

FFmpegOutput::Builder::Builder(Rect sourceSize, PixelFormat sourceFormat, bool isDrmPrime) noexcept
: sourceSize(sourceSize),
  sourceFormat(sourceFormat),
  isSourceDrmPrime(isDrmPrime),
  targetSize(sourceSize),
  codecOptions{}
{
}

FFmpegOutput FFmpegOutput::Builder::build()
{
	if (sourceSize.w == 0 || sourceSize.h == 0)
	{
		throw LibAVException(AVERROR(EINVAL),
		                     "Source frame dimensions must not be zero, got %ux%u", sourceSize.w, sourceSize.h);
	}
	if (targetSize.w == 0 || targetSize.h == 0)
	{
		throw LibAVException(AVERROR(EINVAL),
		                     "Scaled frame dimensions must not be zero, got %ux%u", targetSize.w, targetSize.h);
	}
	if (outputFormat.empty() && outputPath.empty())
	{
		throw LibAVException(AVERROR(EINVAL), "Neither output format nor output path specified");
	}
	if (hwDevicePath.empty())
	{
		throw LibAVException(AVERROR(EINVAL), "No hardware device path specified");
	}

	initFFmpeg();

	int r;
	AVBufferRef* drmDevice {};
	AVBufferRef* vaapiDevice {};

	try
	{
		r = av_hwdevice_ctx_create(&drmDevice, AV_HWDEVICE_TYPE_DRM, hwDevicePath.c_str(), nullptr, 0);
		if (r)
			throw LibAVException(r, "Opening the DRM node %s failed", hwDevicePath.c_str());

		r = av_hwdevice_ctx_create_derived(&vaapiDevice, AV_HWDEVICE_TYPE_VAAPI, drmDevice, 0);
		if (r < 0)
			throw LibAVException(r, "Creating a VAAPI device from DRM node failed");

		auto encoder = std::make_unique<ThreadedVAAPIEncoder>(targetSize.w, targetSize.h, codecOptions, vaapiDevice);

		auto muxer = std::make_unique<Muxer>(outputPath,
				outputFormat, encoder->unwrap().getCodecContext());

		auto scaler = std::make_unique<ThreadedVAAPIScaler>(sourceSize,
				pixelFormat2AV(sourceFormat), targetSize,
				drmDevice, vaapiDevice, isSourceDrmPrime);

		av_buffer_unref(&vaapiDevice);
		av_buffer_unref(&drmDevice);
		return FFmpegOutput(std::move(scaler), std::move(encoder), std::move(muxer));
	}
	catch (...)
	{
		av_buffer_unref(&vaapiDevice);
		av_buffer_unref(&drmDevice);
		throw;
	}
}

}