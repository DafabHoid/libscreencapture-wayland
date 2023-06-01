/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_BLOCKINGRINGBUFFER_HPP
#define SCREENCAPTURE_BLOCKINGRINGBUFFER_HPP

#include <vector>
#include <mutex>
#include <condition_variable>
#include <variant>

template <typename T, size_t Capacity>
class BlockingRingbuffer
{
	std::vector<T> ringBuffer;
	size_t tailIndex = 0;
	std::mutex mutex;
	std::condition_variable readySignal;
	bool eof = false;

public:
	struct EndOfBuffer{};

	size_t size() const noexcept
	{
		return ringBuffer.size() - tailIndex;
	}

	void enqueue(T&& val) noexcept(noexcept(T(std::move(std::declval<T>()))))
	{
		{
			std::lock_guard lock(mutex);
			ringBuffer.push_back(std::move(val));
			if (size() > Capacity)
			{
				T discard = std::move(ringBuffer[tailIndex]);
				++tailIndex;
			}
		}
		readySignal.notify_all();
	}

	std::variant<T, EndOfBuffer> dequeue() noexcept
	{
		std::unique_lock lock(mutex);
		while (size() == 0 && !eof)
		{
			readySignal.wait(lock);
		}
		if (eof)
			[[unlikely]]
			return EndOfBuffer{};
		T val = std::move(ringBuffer[tailIndex]);
		++tailIndex;
		if (tailIndex > std::max(Capacity, size_t(128)))
		{
			ringBuffer.erase(ringBuffer.begin(), ringBuffer.begin() + tailIndex);
			tailIndex = 0;
		}
		return val;
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
