/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "VAAPIScaler.hpp"
#include "libavcommon.hpp"
#include <cstdio>
#include <chrono>
using namespace std::string_literals;

extern "C"
{
#include <libavutil/opt.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
}

using namespace std::chrono_literals;

namespace ffmpeg
{

VAAPIScaler::VAAPIScaler(Rect sourceSize, AVPixelFormat sourceFormat, Rect targetSize,
                         AVBufferRef* drmDevice, AVBufferRef* vaapiDevice, bool inputIsDRMPrime, ScalingDoneCallback cb)
: filterGraph(avfilter_graph_alloc()),
  // DRM PRIME frames can be directly mapped to VAAPI. Memory frames have to be copied to the GPU first.
  hardwareFrameFilterName(inputIsDRMPrime ? "hwmap" : "hwupload"),
  scalingDone(std::move(cb))
{
	const AVFilter* buffersrc = avfilter_get_by_name("buffer");
	const AVFilter* buffersink = avfilter_get_by_name("buffersink");

	if (!inputIsDRMPrime)
	{
		// fix for hwupload on intel graphics:
		// libva-intel claims to not support BGRA but only BGR0, even though
		// libva-intel can import BGRA via DMABuf
		if (sourceFormat == AV_PIX_FMT_BGRA)
			sourceFormat = AV_PIX_FMT_BGR0;
	}

	char args[128];
	std::snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=1/%lld:pixel_aspect=1/1",
	              sourceSize.w, sourceSize.h,
	              inputIsDRMPrime ? AV_PIX_FMT_DRM_PRIME : sourceFormat,
	              std::chrono::duration_cast<std::chrono::microseconds>(1s).count());
	int ret = avfilter_graph_create_filter(&filterSrcContext, buffersrc, "in", args, nullptr, filterGraph);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to create filter graph input");
	}

	ret = avfilter_graph_create_filter(&filterSinkContext, buffersink, "out", nullptr, nullptr, filterGraph);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to create filter graph output");
	}

	AVPixelFormat allowedOutputPixFormats[] = { AV_PIX_FMT_VAAPI, AV_PIX_FMT_NONE };
	ret = av_opt_set_int_list(filterSinkContext, "pix_fmts", allowedOutputPixFormats, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to set output pixel format");
	}

	if (inputIsDRMPrime)
	{
		AVBufferRef* hwFramesContext = av_hwframe_ctx_alloc(drmDevice);

		AVHWFramesContext* hwCtx = reinterpret_cast<AVHWFramesContext*>(hwFramesContext->data);
		hwCtx->format = AV_PIX_FMT_DRM_PRIME;
		hwCtx->sw_format = sourceFormat;
		hwCtx->width = sourceSize.w;
		hwCtx->height = sourceSize.h;
		ret = av_hwframe_ctx_init(hwFramesContext);
		if (ret < 0)
		{
			av_buffer_unref(&hwFramesContext);
			throw LibAVException(ret, "Initializing GPU frame pool failed");
		}

		AVBufferSrcParameters* srcParams = av_buffersrc_parameters_alloc();
		srcParams->hw_frames_ctx = hwFramesContext;
		av_buffersrc_parameters_set(filterSrcContext, srcParams);
		av_free(srcParams);
		av_buffer_unref(&hwFramesContext);
	}

	AVFilterInOut* inputs = avfilter_inout_alloc();
	AVFilterInOut* outputs = avfilter_inout_alloc();

	// connect existing pad (sourced by buffersrc) to the unconnected input "in" in the parsed graph
	outputs->name = av_strdup("in");
	outputs->filter_ctx = filterSrcContext;
	outputs->pad_idx = 0;
	outputs->next = nullptr;

	// connect existing pad (goes to buffersink) to the unconnected output "out" in the parsed graph
	inputs->name = av_strdup("out");
	inputs->filter_ctx = filterSinkContext;
	inputs->pad_idx = 0;
	inputs->next = nullptr;

	char filterGraphDesc[128];
	std::snprintf(filterGraphDesc, sizeof(filterGraphDesc),
	              "%s,scale_vaapi=w=%d:h=%d:format=nv12:out_range=full",
	              hardwareFrameFilterName, targetSize.w, targetSize.h);
	ret = avfilter_graph_parse_ptr(filterGraph, filterGraphDesc, &inputs, &outputs, nullptr);
	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to parse filter graph");
	}
	AVFilterContext* hardwareFrameFilter = avfilter_graph_get_filter(filterGraph, ("Parsed_"s + hardwareFrameFilterName + "_0").c_str());
	if (!hardwareFrameFilter)
		throw LibAVException(AVERROR(EINVAL), "could not find %s", hardwareFrameFilterName);
	hardwareFrameFilter->hw_device_ctx = av_buffer_ref(vaapiDevice);

	ret = avfilter_graph_config(filterGraph, nullptr);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to configure filter graph");
	}

	scaleThread = std::thread([this] () { scaleFramesLoop(); });
}

VAAPIScaler::~VAAPIScaler() noexcept
{
	scaleQueue.signalEOF();
	if (scaleThread.joinable())
		scaleThread.join();
	AVFilterContext* hardwareFrameFilter = avfilter_graph_get_filter(filterGraph, ("Parsed_"s + hardwareFrameFilterName + "_0").c_str());
	if (hardwareFrameFilter)
	{
		av_buffer_unref(&hardwareFrameFilter->hw_device_ctx);
	}
	if (filterSrcContext)
	{
		av_buffer_unref(&filterSrcContext->hw_device_ctx);
	}
	avfilter_graph_free(&filterGraph);
}

void VAAPIScaler::scaleFrame(AVFrame& frame)
{
	int err = av_buffersrc_add_frame_flags(filterSrcContext, &frame, 0);
	if (err)
		throw LibAVException(err, "Inserting frame into filter failed");

	while (true)
	{
		auto gpuFrame = AVFrame_Heap(av_frame_alloc());
		err = av_buffersink_get_frame(filterSinkContext, gpuFrame.get());
		if (err == AVERROR(EAGAIN) || err == AVERROR(ENOMEM))
		{
			break;
		}
		if (err < 0)
		{
			throw LibAVException(err, "Extracting frame from filter failed");
		}
		scalingDone(std::move(gpuFrame));
	}
}

void VAAPIScaler::enqueueFrame(AVFrame_Heap frame)
{
	if (scaleThreadException)
		std::rethrow_exception(scaleThreadException);
	// fix for hwupload on intel graphics
	if (frame->format == AV_PIX_FMT_BGRA)
		frame->format = AV_PIX_FMT_BGR0;
	scaleQueue.enqueue(std::move(frame));
}

std::variant<AVFrame_Heap, VAAPIScaler::EndOfQueue> VAAPIScaler::dequeueFrame()
{
	auto frameOrEnd = scaleQueue.dequeue();
	if (std::holds_alternative<AVFrame_Heap>(frameOrEnd))
		return std::move(std::get<AVFrame_Heap>(frameOrEnd));
	return EndOfQueue{};
}

void VAAPIScaler::scaleFramesLoop()
{
	try
	{
		while (true)
		{
			auto frameOrEnd = dequeueFrame();
			if (std::holds_alternative<EndOfQueue>(frameOrEnd))
				break;
			auto& frame = std::get<AVFrame_Heap>(frameOrEnd);
			scaleFrame(*frame);
		}
	}
	catch (const std::exception& e)
	{
		scaleThreadException = std::current_exception();
	}
}

}