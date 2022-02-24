/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_BLOCKINGRINGBUFFER_HPP
#define SCREENCAPTURE_BLOCKINGRINGBUFFER_HPP

#include <queue>
#include <list>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <variant>

using namespace std::chrono_literals;

template <typename T, size_t Capacity>
class BlockingRingbuffer
{
	std::queue<T, std::list<T>> ringBuffer;
	std::mutex mutex;
	std::condition_variable readySignal;
	bool eof = false;

public:
	struct EndOfBuffer{};

	void enqueue(T&& val)
	{
		{
			std::lock_guard lock(mutex);
			if (ringBuffer.size() >= Capacity)
				return;
			ringBuffer.push(std::move(val));
		}
		readySignal.notify_all();
	}

	std::variant<T, EndOfBuffer> dequeue()
	{
		std::unique_lock lock(mutex);
		while (ringBuffer.empty() && !eof)
		{
			readySignal.wait_for(lock, 1s);
		}
		if (eof)
			return EndOfBuffer{};
		auto v = std::move(ringBuffer.front());
		ringBuffer.pop();
		return v;
	}

	void signalEOF() noexcept
	{
		{
			std::lock_guard lock(mutex);
			eof = true;
		}
		readySignal.notify_all();
	}
};


#endif //SCREENCAPTURE_BLOCKINGRINGBUFFER_HPP
