/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_THREADEDWRAPPER_HPP
#define SCREENCAPTURE_THREADEDWRAPPER_HPP

#include "libavcommon.hpp"
#include "BlockingRingbuffer.hpp"
#include <thread>
#include <exception>

namespace ffmpeg
{

template <typename FrameProcessor>
using FrameProcessMethod = void (FrameProcessor::*)(AVFrame&);

template <typename FrameProcessor, FrameProcessMethod<FrameProcessor> processMethod>
class ThreadedWrapper
{
	BlockingRingbuffer<AVFrame_Heap, 4> queue;
	std::thread thread;
	std::exception_ptr threadException;

	FrameProcessor wrapped;

	/** Take continuously frames from the queue and process them with the @em FrameProcessor by calling
	 * its @em processMethod.*/
	void processFramesLoop() noexcept;

	SCW_EXPORT void init();

public:
	template <typename... Args>
	SCW_EXPORT ThreadedWrapper(Args&&... args)
	: wrapped(std::forward<Args>(args)...)
	{
		init();
	}

	/** Signal the thread to stop, wait for it to terminate and then destroy the wrapped object. */
	SCW_EXPORT ~ThreadedWrapper() noexcept;

	/** Get access to the wrapped object. */
	SCW_EXPORT FrameProcessor& unwrap() noexcept { return wrapped; }

	/** Add a frame into the thread queue. It will be taken by this object's thread in processFramesLoop().
	 * If the queue is full, the frame will by silently dropped.
	 * Should the thread previously have thrown an exception, it is rethrown here.
	 * This function is thread-safe. */
	SCW_EXPORT void processFrame(AVFrame_Heap frame);
};

}


#endif //SCREENCAPTURE_THREADEDWRAPPER_HPP
