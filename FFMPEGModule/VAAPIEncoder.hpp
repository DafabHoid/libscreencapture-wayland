/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_VAAPIENCODER_HPP
#define SCREENCAPTURE_VAAPIENCODER_HPP

#include "libavcommon.hpp"
#include <functional>
#include "ThreadedWrapper.hpp"
extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace ffmpeg
{


class SCW_EXPORT VAAPIEncoder
{
	const AVCodec* codec;
	AVCodecContext* codecContext;
	AVPacket* encodedFrame;

public:
	using EncodedCallback = std::function<void(AVPacket&)>;

	using CallbackType = EncodedCallback;

	VAAPIEncoder(unsigned int width, unsigned int height, AVDictionary* codecOptions, AVBufferRef* hwDevice);
	VAAPIEncoder(VAAPIEncoder&&) noexcept;
	VAAPIEncoder(const VAAPIEncoder&) = delete;
	~VAAPIEncoder() noexcept;

	void encodeFrame(AVFrame& gpuFrame, const EncodedCallback& encodingDone);

	inline void processFrame(AVFrame& frame, const EncodedCallback& encodingDone)
	{ encodeFrame(frame, encodingDone); }

	const AVCodec* getCodec() const noexcept { return codec; }
	const AVCodecContext* getCodecContext() const noexcept { return codecContext; }
};


using ThreadedVAAPIEncoder = ThreadedWrapper<VAAPIEncoder>;

}


#endif //SCREENCAPTURE_VAAPIENCODER_HPP
