/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "VAAPIEncoder.hpp"
#include "libavcommon.hpp"
#include <chrono>

using namespace std::chrono_literals;

namespace ffmpeg
{

VAAPIEncoder::VAAPIEncoder(unsigned int width, unsigned int height, AVBufferRef* hwDevice)
: encodedFrame(av_packet_alloc())
{
	codec = avcodec_find_encoder_by_name("h264_vaapi");
	if (codec == nullptr)
		throw LibAVException(AVERROR(ENXIO), "no encoder named \"h264_vaapi\" found");

	AVBufferRef* hwFramesContext = av_hwframe_ctx_alloc(hwDevice);
	AVHWFramesContext* hwCtx = reinterpret_cast<AVHWFramesContext*>(hwFramesContext->data);
	hwCtx->format = AV_PIX_FMT_VAAPI;
	hwCtx->sw_format = AV_PIX_FMT_NV12;
	hwCtx->width = width;
	hwCtx->height = height;
	int r = av_hwframe_ctx_init(hwFramesContext);
	if (r)
		throw LibAVException(r, "Initializing GPU frame pool failed");

	codecContext = avcodec_alloc_context3(codec);
	codecContext->width = width;
	codecContext->height = height;
	codecContext->framerate = AVRational {0, 1};
	codecContext->time_base = AVRational {1, std::chrono::duration_cast<std::chrono::microseconds>(1s).count()};
	codecContext->sample_aspect_ratio = AVRational {1, 1};
	codecContext->color_range = AVCOL_RANGE_JPEG;
	codecContext->gop_size = 30;
	codecContext->bit_rate = 4*1000*1000;
	codecContext->pix_fmt = AV_PIX_FMT_VAAPI;
	codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // provide codecContext->extradata for muxer instead of inside the packets
	codecContext->hw_frames_ctx = hwFramesContext;

	r = avcodec_open2(codecContext, codec, nullptr);
	if (r)
		throw LibAVException(r, "Opening encoder failed");
}

VAAPIEncoder::VAAPIEncoder(VAAPIEncoder&& o) noexcept
: codec(o.codec),
  codecContext(o.codecContext),
  encodedFrame(o.encodedFrame)
{
	o.encodedFrame = nullptr;
	o.codecContext = nullptr;
}

VAAPIEncoder::~VAAPIEncoder() noexcept
{
	av_packet_free(&encodedFrame);
	avcodec_free_context(&codecContext);
}

void VAAPIEncoder::encodeFrame(AVFrame& gpuFrame, const EncodedCallback& encodedCallback)
{
	int err = avcodec_send_frame(codecContext, &gpuFrame);
	if (err < 0)
		throw LibAVException(err, "Encoding failed");
	AVPacket* p = encodedFrame;
	while (true) {
		err = avcodec_receive_packet(codecContext, p);
		if (err == AVERROR(EAGAIN) || err == AVERROR(AVERROR_EOF))
			break;
		if (err < 0)
			throw LibAVException(err, "Extracting frame from encoder failed");

		av_log(nullptr, AV_LOG_VERBOSE, "Frame encoded, pts: %lx\n", p->pts);
		encodedCallback(*p);
		av_packet_unref(p);
	}
}

}

#include "ThreadedWrapper.inc"

namespace ffmpeg
{
// instantiate code for template
template class ThreadedWrapper<VAAPIEncoder>;
}