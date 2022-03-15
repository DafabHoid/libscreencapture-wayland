/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "ThreadedWrapper.hpp"

namespace ffmpeg
{

template <typename FrameProcessor, FrameProcessMethod<FrameProcessor> processMethod>
void ThreadedWrapper<FrameProcessor, processMethod>::processFramesLoop() noexcept
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
			(wrapped.*processMethod)(*frame);
		}
	}
	catch (const std::exception& e)
	{
		threadException = std::current_exception();
	}
}


template <typename FrameProcessor, FrameProcessMethod<FrameProcessor> processMethod>
void ThreadedWrapper<FrameProcessor, processMethod>::init()
{
	thread = std::thread([this] () { processFramesLoop(); });
}


template <typename FrameProcessor, FrameProcessMethod<FrameProcessor> processMethod>
ThreadedWrapper<FrameProcessor, processMethod>::~ThreadedWrapper() noexcept
{
	queue.signalEOF();
	if (thread.joinable())
	{
		[[likely]]
		thread.join();
	}
}


template <typename FrameProcessor, FrameProcessMethod<FrameProcessor> processMethod>
void ThreadedWrapper<FrameProcessor, processMethod>::processFrame(AVFrame_Heap frame)
{
	if (threadException)
		std::rethrow_exception(threadException);
	queue.enqueue(std::move(frame));
}

}