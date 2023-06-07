/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_PIPEWIRESTREAM_HPP
#define SCREENCAPTURE_PIPEWIRESTREAM_HPP

#include "../common.hpp"
#include <cstdint>
#include <exception>
#include <chrono>
#include <pipewire/pipewire.h>
#include <spa/param/video/format.h>

namespace pw
{
using common::Rect;
using common::PixelFormat;
using common::MemoryFrame;
using common::DmaBufFrame;
using common::SharedScreen;

class StreamCallbacks
{
public:
	/** This callback function is called after the PipeWire stream became connected.
	 * You should use it to set up a consumer that processes the frames given to you by
	 * the processMemoryFrame/processDmaBufFrame functions.
	 *
	 * A stream can either provide the frames in conventional memory or as a reference to GPU memory.
	 * The @em isDmaBuf parameter tells you what type it is. The appropriate pushFrame function is called
	 * for each type.
	 *
	 * Should the callback throw an exception, the stream is disconnected and the exception forwarded to
	 * the caller of PipeWireStream::runStreamLoop().
	 * @param dimensions the width and height of the stream in pixels
	 * @param format the pixel format of each video frame
	 * @param isDmaBuf true if the stream provides DmaBufFrame, false for MemoryFrame */
	virtual void streamConnected(Rect dimensions, PixelFormat format, bool isDmaBuf) = 0;

	/** This callback function is called when the stream is disconnected.
	 * You should stop all ongoing processing of frames here and release all frames that were
	 * previously given to you by a pushFrame call. */
	virtual void streamDisconnected() = 0;

	/** This callback function is called for every frame in the PipeWire stream, if the stream uses
	 * conventional memory. Remember to keep a reference to this frame while using its pixel data.
	 * @param frame the frame data including its dimensions, format and a pointer to pixel data */
	virtual void processMemoryFrame(std::unique_ptr<MemoryFrame> frame) = 0;

	/** This callback function is called for every frame in the PipeWire stream, if the stream uses
	 * DmaBuf (GPU) memory. Remember to keep a reference to this frame while using its file descriptor.
	 * @param frame the frame data including its dimensions, format and a DRM PRIME file descriptor */
	virtual void processDmaBufFrame(std::unique_ptr<DmaBufFrame> frame) = 0;

};

class PipeWireStream;

struct StreamInfo
{
	pw_stream *stream;
	spa_video_info format;
	bool haveDmaBuf;
	std::chrono::time_point<std::chrono::steady_clock> startTime;
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

	StreamCallbacks& streamCallbacks;

	friend void streamStateChanged(void*, pw_stream_state, pw_stream_state, const char*) noexcept;
	friend void processFrame(void*) noexcept;

public:
	/** Create a new PipeWire stream that is connected to the given shared video stream.
	 * To actually start streaming and pushing frames to the later stages, you need to call runStreamLoop().
	 * The given callback interface's methods are called for their respective events. See its documentation for information
	 * about how they are used. */
	SCW_EXPORT PipeWireStream(const SharedScreen& shareInfo, StreamCallbacks& streamCallbacks, bool supportDmaBuf);

	SCW_EXPORT ~PipeWireStream() noexcept;

	/** Run a PipeWire main loop to process all events of this stream.
	 * This function will block as long as the loop is running. It can be stopped by calling quit().
	 * @throw std::exception if an exception was set with setError() while the loop ran */
	SCW_EXPORT void runStreamLoop();

	/** Store the given exception to pass it to the caller of runStreamLoop(), once the
	 * stream loop quits. */
	SCW_EXPORT void setError(std::exception_ptr) noexcept;

	/** Quit the currently running stream loop, so the call to runStreamLoop() returns.
	 * Can be called from any thread. */
	SCW_EXPORT void quit() noexcept;
};

} // namespace pw

#endif //SCREENCAPTURE_PIPEWIRESTREAM_HPP