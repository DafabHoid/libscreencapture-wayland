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


class VAAPIEncoder
{
	const AVCodec* codec;
	AVCodecContext* codecContext;
	AVPacket* encodedFrame;

public:
	using EncodedCallback = std::function<void(AVPacket&)>;

	using CallbackType = EncodedCallback;

	SCW_EXPORT VAAPIEncoder(unsigned int width, unsigned int height, AVDictionary* codecOptions, AVBufferRef* hwDevice);
	SCW_EXPORT VAAPIEncoder(VAAPIEncoder&&) noexcept;
	           VAAPIEncoder(const VAAPIEncoder&) = delete;
	SCW_EXPORT ~VAAPIEncoder() noexcept;

	SCW_EXPORT void encodeFrame(AVFrame& gpuFrame, const EncodedCallback& encodingDone);

	inline void processFrame(AVFrame& frame, const EncodedCallback& encodingDone)
	{ encodeFrame(frame, encodingDone); }

	SCW_EXPORT const AVCodec* getCodec() const noexcept { return codec; }
	SCW_EXPORT const AVCodecContext* getCodecContext() const noexcept { return codecContext; }
};


using ThreadedVAAPIEncoder = ThreadedWrapper<VAAPIEncoder>;

// declare external instantiation for template
extern template class ThreadedWrapper<VAAPIEncoder>;

}


#endif //SCREENCAPTURE_VAAPIENCODER_HPP
