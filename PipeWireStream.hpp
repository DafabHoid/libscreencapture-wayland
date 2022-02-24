/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_PIPEWIRESTREAM_HPP
#define SCREENCAPTURE_PIPEWIRESTREAM_HPP

#include "common.hpp"
#include <cstdint>
#include <exception>
#include <pipewire/pipewire.h>
#include <spa/param/video/format.h>

namespace pw
{
class PipeWireStream;

struct StreamInfo
{
	pw_stream *stream;
	spa_video_info format;
	bool haveDmaBuf;
	struct
	{
		int32_t x;
		int32_t y;
	} cursorPos;
	struct
	{
		uint32_t w;
		uint32_t h;
		uint8_t *bitmap;
	} cursorBitmap;
	PipeWireStream *mainLoopInfo;
};

class PipeWireStream
{
	pw_main_loop *mainLoop;
	pw_context *ctx;
	pw_core *core;
	StreamInfo streamInfo;
	spa_hook coreListener;
	std::exception_ptr streamException;

public:
	/** Create a new PipeWire stream that is connected to the given shared video stream.
	 * To actually start streaming and pushing frames to the later stages, you need to call runStreamLoop(). */
	PipeWireStream(const SharedScreen& shareInfo);

	~PipeWireStream() noexcept;

	/** Run a PipeWire main loop to process all events of this stream.
	 * This function will block as long as the loop is running. It can be stopped by calling quit().
	 * @throw std::exception if an exception was set with setError() while the loop ran */
	void runStreamLoop();

	/** Store the given exception to pass it to the caller of runStreamLoop(), once the
	 * stream loop quits. */
	void setError(std::exception_ptr) noexcept;

	/** Quit the currently running stream loop, so the call to runStreamLoop() returns.
	 * Can be called from any thread. */
	void quit() noexcept;
};

} // namespace pw

#endif //SCREENCAPTURE_PIPEWIRESTREAM_HPP