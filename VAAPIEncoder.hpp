/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_VAAPIENCODER_HPP
#define SCREENCAPTURE_VAAPIENCODER_HPP

#include "libavcommon.hpp"
#include <thread>
#include "BlockingRingbuffer.hpp"
#include <functional>
extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace ffmpeg
{

using EncodedCallback = std::function<void(AVPacket&)>;

class VAAPIEncoder
{
	const AVCodec* codec;
	AVCodecContext* codecContext;
	AVPacket* encodedFrame;
	EncodedCallback encodedCallback;
	BlockingRingbuffer<AVFrame_Heap, 8> encodeQueue;
	std::thread encodingThread;
	std::exception_ptr encodingThreadException;
	void encodeFramesLoop();

public:
	SCW_EXPORT VAAPIEncoder(unsigned int width, unsigned int height, AVBufferRef* hwDevice, EncodedCallback cb);
	SCW_EXPORT ~VAAPIEncoder() noexcept;

	SCW_EXPORT void enqueueFrame(AVFrame_Heap frame);
	struct EndOfQueue {};
	SCW_EXPORT std::variant<AVFrame_Heap, EndOfQueue> dequeueFrame();

	SCW_EXPORT void encodeFrame(AVFrame& gpuFrame);

	SCW_EXPORT const AVCodec* getCodec() const noexcept { return codec; }
	SCW_EXPORT const AVCodecContext* getCodecContext() const noexcept { return codecContext; }
};

}


#endif //SCREENCAPTURE_VAAPIENCODER_HPP
