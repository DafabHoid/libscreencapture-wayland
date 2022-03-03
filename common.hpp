/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_COMMON_HPP
#define SCREENCAPTURE_COMMON_HPP

#include <cstdint>
#include <functional>
#include <memory> // unique_ptr
#include <optional>




#include <sdbus-c++/IConnection.h>

struct SharedScreen
{
	/** The D-Bus connection through which the shared screen has been requested.
	 * Deleting this pointer will close the connection and automatically invalidate #pipeWireNode,
	 * so keep it open as long as you need the shared screen. */
	std::unique_ptr<sdbus::IConnection> dbusConnection;

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

struct DmaBufFrame
{
	uint32_t width;
	uint32_t height;
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
};
struct MemoryFrame
{
	uint32_t width;
	uint32_t height;
	PixelFormat format;
	void* memory;
	size_t stride;
	size_t size;
	size_t offset;
};
using FrameDoneCallback = std::function<void()>;



extern void dumpStackTrace(const char* filename = "trace.txt") noexcept;



#endif //SCREENCAPTURE_COMMON_HPP