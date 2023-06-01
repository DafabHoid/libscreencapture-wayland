/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "Muxer.hpp"
#include "libavcommon.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace ffmpeg
{

Muxer::Muxer(const std::string& outputURL, const std::string& containerFormat, const AVCodecContext* videoCodecCtx)
: outputVideoStream{},
  codecTimeBase(videoCodecCtx->time_base)
{
	int r = avformat_alloc_output_context2(&formatContext, nullptr, containerFormat.c_str(), nullptr);
	if (r)
		throw LibAVException(r, "Allocating an output context for MPEG-TS failed");

	if ((formatContext->oformat->flags & AVFMT_NOFILE) != 0)
	{
		formatContext->url = av_strdup(outputURL.c_str());
	}
	else
	{
		r = avio_open(&formatContext->pb, outputURL.c_str(), AVIO_FLAG_WRITE);
		if (r < 0)
		{
			throw LibAVException(r, "Opening output file failed");
		}
	}

	outputVideoStream = avformat_new_stream(formatContext, nullptr);
	outputVideoStream->id = 0;
	r = avcodec_parameters_from_context(outputVideoStream->codecpar, videoCodecCtx);
	if (r < 0)
		throw LibAVException(r, "Copying codec parameters failed");
	outputVideoStream->codecpar->format = AV_PIX_FMT_YUV420P;

	av_dump_format(formatContext, 0, outputURL.c_str(), 1);

	r = avformat_init_output(formatContext, nullptr);
	if (r < 0)
		throw LibAVException(r, "Initializing muxer failed");

	r = avformat_write_header(formatContext, nullptr);
	if (r < 0)
		throw LibAVException(r, "Writing container header failed");
}

Muxer::~Muxer() noexcept
{
	if (formatContext)
	{
		if (outputVideoStream)
			av_write_trailer(formatContext);
		avio_closep(&formatContext->pb);
		avformat_free_context(formatContext);
	}
}

void Muxer::writePacket(AVPacket& p)
{
	p.stream_index = outputVideoStream->index;
	av_packet_rescale_ts(&p, codecTimeBase, outputVideoStream->time_base);
	int err = av_interleaved_write_frame(formatContext, &p);
	if (err < 0)
		throw LibAVException(err, "Writing packet failed");
}

}