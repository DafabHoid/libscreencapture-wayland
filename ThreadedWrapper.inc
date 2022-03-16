/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "ThreadedWrapper.hpp"

namespace ffmpeg
{

template <typename FrameProcessor, typename OutputType, FrameProcessMethod<FrameProcessor, OutputType> processMethod>
void ThreadedWrapper<FrameProcessor, OutputType, processMethod>::processFramesLoop() noexcept
{
	try
	{
		while (true)
		{
			auto frameOrEnd = queue.dequeue();
			if (!std::holds_alternative<AVFrame_Heap>(frameOrEnd))
				[[unlikely]]
				break;
			auto& frame = std::get<AVFrame_Heap>(frameOrEnd);
			(wrapped.*processMethod)(*frame, frameProcessedCallback);
		}
	}
	catch (const std::exception& e)
	{
		threadException = std::current_exception();
	}
}


template <typename FrameProcessor, typename OutputType, FrameProcessMethod<FrameProcessor, OutputType> processMethod>
void ThreadedWrapper<FrameProcessor, OutputType, processMethod>::init()
{
	thread = std::thread([this] () { processFramesLoop(); });
}


template <typename FrameProcessor, typename OutputType, FrameProcessMethod<FrameProcessor, OutputType> processMethod>
ThreadedWrapper<FrameProcessor, OutputType, processMethod>::~ThreadedWrapper() noexcept
{
	queue.signalEOF();
	if (thread.joinable())
	{
		[[likely]]
		thread.join();
	}
}


template <typename FrameProcessor, typename OutputType, FrameProcessMethod<FrameProcessor, OutputType> processMethod>
void ThreadedWrapper<FrameProcessor, OutputType, processMethod>::processFrame(AVFrame_Heap frame)
{
	if (threadException)
		std::rethrow_exception(threadException);
	queue.enqueue(std::move(frame));
}


template <typename FrameProcessor, typename OutputType, FrameProcessMethod<FrameProcessor, OutputType> processMethod>
void ThreadedWrapper<FrameProcessor, OutputType, processMethod>::setFrameProcessedCallback(
		FrameProcessedCallback <OutputType> cb) noexcept
{
	frameProcessedCallback = std::move(cb);
}

}