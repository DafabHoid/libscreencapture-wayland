/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_COMMON_HPP
#define SCREENCAPTURE_COMMON_HPP

#include <cstdint>
#include <functional>
#include <memory> // shared_ptr
#include <optional>
#include <chrono>

#define SCW_EXPORT [[gnu::visibility("default")]]


namespace sdbus
{
class IConnection;
}

namespace common
{

struct SharedScreen
{
	/** The D-Bus connection through which the shared screen has been requested.
	 * Deleting this pointer will close the connection and automatically invalidate #pipeWireNode,
	 * so keep it open as long as you need the shared screen. */
	std::shared_ptr<sdbus::IConnection> dbusConnection;

	/** file descriptor where the PipeWire server can be reached */
	int pipeWireFd;

	/** PipeWire node ID of the video stream for the shared screen */
	uint32_t pipeWireNode;
};

struct Rect
{
	unsigned int w;
	unsigned int h;
};

enum class PixelFormat
{
	BGRA,
	RGBA,
	BGRX,
	RGBX,
};

using FrameDoneCallback = std::function<void()>;

struct DmaBufFrame
{
	uint32_t width;
	uint32_t height;
	std::chrono::nanoseconds pts;
	uint64_t drmFormat;
	struct {
		int fd;
		size_t totalSize;
		uint64_t modifier;
	} drmObject;
	uint32_t planeCount;
	struct {
		size_t offset;
		size_t pitch;
	} planes[4];

	FrameDoneCallback onFrameDone;

	DmaBufFrame() = default;
	DmaBufFrame(const DmaBufFrame&) = delete;
	DmaBufFrame(DmaBufFrame&&) = default;

	~DmaBufFrame() noexcept
	{
		onFrameDone();
	}
};
struct MemoryFrame
{
	uint32_t width;
	uint32_t height;
	std::chrono::nanoseconds pts;
	PixelFormat format;
	void* memory;
	size_t stride;
	size_t size;
	size_t offset;

	FrameDoneCallback onFrameDone;

	MemoryFrame() = default;
	MemoryFrame(const MemoryFrame&) = delete;
	MemoryFrame(MemoryFrame&&) = default;

	~MemoryFrame() noexcept
	{
		onFrameDone();
	}
};

}



SCW_EXPORT extern void dumpStackTrace(const char* filename = "trace.txt") noexcept;



#endif //SCREENCAPTURE_COMMON_HPP