/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_COMMON_HPP
#define SCREENCAPTURE_COMMON_HPP

#include <cstdint>
#include <functional>
#include <memory> // shared_ptr
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



#ifndef NDEBUG
#include <execinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
/** Dump a stack trace to the file at @param filename.
 * This function does not use the heap, and only opens a file descriptor for the output file. */
inline void dumpStackTrace(const char* filename = "trace.txt") noexcept
{
	void* bt[50];
	int num = backtrace(bt, sizeof(bt)/sizeof(bt[0]));
	// write the stack trace if it was successful and includes more than this function's frame
	if (num > 1)
	{
		int fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
		if (fd < 0)
			perror("Opening trace file failed");
		else
		{
			const char* msg = "Trace for Exception:\n";
			write(fd, msg, sizeof(msg)-1);
			// write the stack trace, starting from the frame of our calling function
			backtrace_symbols_fd(bt + 1, num - 1, fd);
			close(fd);
		}
	}
}
#endif



#endif //SCREENCAPTURE_COMMON_HPP