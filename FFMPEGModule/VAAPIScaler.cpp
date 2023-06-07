/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "VAAPIScaler.hpp"
#include "libavcommon.hpp"
#include <cstdio>
#include <chrono>
#include <string>
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
                         AVBufferRef* drmDevice, AVBufferRef* vaapiDevice, bool inputIsDRMPrime)
: filterGraph(avfilter_graph_alloc()),
  // DRM PRIME frames can be directly mapped to VAAPI. Memory frames have to be copied to the GPU first.
  hardwareFrameFilterName(inputIsDRMPrime ? "hwmap" : "hwupload")
{
	const AVFilter* buffersrc = avfilter_get_by_name("buffer");
	const AVFilter* buffersink = avfilter_get_by_name("buffersink");

	// create source for filter graph
	// the arguments provide information to the graph about what its input will look like
	char args[128];
	std::snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=1/%" PRIu64 ":pixel_aspect=1/1",
	              sourceSize.w, sourceSize.h,
	              inputIsDRMPrime ? AV_PIX_FMT_DRM_PRIME : sourceFormat,
	              std::chrono::duration_cast<std::chrono::microseconds>(1s).count());
	int ret = avfilter_graph_create_filter(&filterSrcContext, buffersrc, "in", args, nullptr, filterGraph);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to create filter graph input");
	}

	// create sink for filter graph
	ret = avfilter_graph_create_filter(&filterSinkContext, buffersink, "out", nullptr, nullptr, filterGraph);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to create filter graph output");
	}

	// constrain the allowed pixel format on the graph output
	AVPixelFormat allowedOutputPixFormats[] = { AV_PIX_FMT_VAAPI, AV_PIX_FMT_NONE };
	ret = av_opt_set_int_list(filterSinkContext, "pix_fmts", allowedOutputPixFormats, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to set output pixel format");
	}

	if (inputIsDRMPrime)
	{
		// if the input frames are DRM PRIME-allocated, they are on the GPU already
		// → map frames directly to VAAPI
		// → ffmpeg needs a HardwareFramesContext on input frames that associates them with the hardware device
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

		// give this context to the graph input so all input frames are automatically associated with it
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

	// create the filter graph by parsing a description
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

	// the hardware upload/map filter needs the VAAPI device to upload or map the frames to it
	AVFilterContext* hardwareFrameFilter = avfilter_graph_get_filter(filterGraph, ("Parsed_"s + hardwareFrameFilterName + "_0").c_str());
	if (!hardwareFrameFilter)
		throw LibAVException(AVERROR(EINVAL), "could not find %s", hardwareFrameFilterName);
	hardwareFrameFilter->hw_device_ctx = av_buffer_ref(vaapiDevice);

	// configure the graph to ensure all nodes correctly connect to each other
	ret = avfilter_graph_config(filterGraph, nullptr);
	if (ret < 0)
	{
		throw LibAVException(ret, "Failed to configure filter graph");
	}
}

VAAPIScaler::VAAPIScaler(VAAPIScaler&& o) noexcept
: filterGraph(o.filterGraph),
  filterSrcContext(o.filterSrcContext),
  filterSinkContext(o.filterSinkContext),
  hardwareFrameFilterName(o.hardwareFrameFilterName)
{
	o.filterSrcContext = nullptr;
	o.filterSinkContext = nullptr;
	o.filterGraph = nullptr;
}

VAAPIScaler::~VAAPIScaler() noexcept
{
	if (filterGraph)
	{
		AVFilterContext* hardwareFrameFilter =
				avfilter_graph_get_filter(filterGraph,("Parsed_"s + hardwareFrameFilterName + "_0").c_str());
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
}

void VAAPIScaler::scaleFrame(AVFrame& frame, const ScalingDoneCallback& scalingDone)
{
	int err = av_buffersrc_add_frame_flags(filterSrcContext, &frame, 0);
	if (err)
		throw LibAVException(err, "Inserting frame into filter failed");

	while (true)
	{
		auto gpuFrame = AVFrame_Heap(av_frame_alloc());
		err = av_buffersink_get_frame(filterSinkContext, gpuFrame.get());
		if (err == AVERROR(EAGAIN))
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

}

#include "ThreadedWrapper.inc"

namespace ffmpeg
{
// instantiate code for template
template class ThreadedWrapper<VAAPIScaler>;
}