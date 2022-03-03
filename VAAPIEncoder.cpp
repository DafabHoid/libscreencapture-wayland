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

VAAPIEncoder::VAAPIEncoder(unsigned int width, unsigned int height, AVBufferRef* hwDevice, EncodedCallback cb)
: encodedFrame(av_packet_alloc()),
  encodedCallback(std::move(cb))
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
	codecContext->global_quality = 25;
	codecContext->keyint_min = 10;
	codecContext->pix_fmt = AV_PIX_FMT_VAAPI;
	codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // provide codecContext->extradata for muxer instead of inside the packets
	codecContext->hw_frames_ctx = hwFramesContext;

	r = avcodec_open2(codecContext, codec, nullptr);
	if (r)
		throw LibAVException(r, "Opening encoder failed");

	encodingThread = std::thread([this] () { encodeFramesLoop(); });
}

VAAPIEncoder::~VAAPIEncoder() noexcept
{
	encodeQueue.signalEOF();
	if (encodingThread.joinable())
	{
		encodingThread.join();
	}
	av_packet_free(&encodedFrame);
	avcodec_free_context(&codecContext);
}

void VAAPIEncoder::enqueueFrame(AVFrame_Heap frame)
{
	if (encodingThreadException)
		std::rethrow_exception(encodingThreadException);
	encodeQueue.enqueue(std::move(frame));
}

std::variant<AVFrame_Heap, VAAPIEncoder::EndOfQueue> VAAPIEncoder::dequeueFrame()
{
	auto frameOrEnd = encodeQueue.dequeue();
	if (std::holds_alternative<AVFrame_Heap>(frameOrEnd))
		return std::move(std::get<AVFrame_Heap>(frameOrEnd));
	return EndOfQueue{};
}

void VAAPIEncoder::encodeFrame(AVFrame& gpuFrame)
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

void VAAPIEncoder::encodeFramesLoop()
{
	try
	{
		while (true)
		{
			auto frameOrEnd = dequeueFrame();
			if (std::holds_alternative<EndOfQueue>(frameOrEnd))
				break;
			auto& frame = std::get<AVFrame_Heap>(frameOrEnd);
			encodeFrame(*frame);
		}
	}
	catch (const std::exception& e)
	{
		encodingThreadException = std::current_exception();
	}
}

}