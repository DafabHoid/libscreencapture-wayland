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
	VAAPIEncoder(unsigned int width, unsigned int height, AVBufferRef* hwDevice, EncodedCallback cb);
	~VAAPIEncoder() noexcept;

	void enqueueFrame(AVFrame_Heap frame);
	struct EndOfQueue {};
	std::variant<AVFrame_Heap, EndOfQueue> dequeueFrame();

	void encodeFrame(AVFrame& gpuFrame);

	const AVCodec* getCodec() const noexcept { return codec; }
	const AVCodecContext* getCodecContext() const noexcept { return codecContext; }
};

}


#endif //SCREENCAPTURE_VAAPIENCODER_HPP
