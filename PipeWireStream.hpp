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
/** This callback function is called after the PipeWire stream became connected.
 * You should use it to set up a consumer that processes the frames given to you by
 * the PushMemoryFrame/PushDmaBufFrame callbacks.
 *
 * A stream can either provide the frames in conventional memory or as a reference to GPU memory.
 * The @em isDmaBuf parameter tells you what type it is. The appropriate PushFrame callback is called
 * for each type.
 *
 * Should the callback throw an exception, the stream is disconnected and the exception forwarded to
 * the caller of PipeWireStream::runStreamLoop().
 * @param dimensions the width and height of the stream in pixels
 * @param format the pixel format of each video frame
 * @param isDmaBuf true if the stream provides DmaBufFrame, false for MemoryFrame */
using StreamConnectedCallback = std::function<void(Rect dimensions, PixelFormat format, bool isDmaBuf)>;

/** This callback function is called for every frame in the PipeWire stream, if the stream uses
 * conventional memory.
 * @param frame the frame data including its dimensions, format and a pointer to pixel data
 * @param cb a callback function to call when you are done using the frame data, to release its resources */
using PushMemoryFrameCallback = std::function<void(const MemoryFrame& frame, FrameDoneCallback cb)>;

/** This callback function is called for every frame in the PipeWire stream, if the stream uses
 * DmaBuf (GPU) memory.
 * @param frame the frame data including its dimensions, format and a DRM PRIME file descriptor
 * @param cb a callback function to call when you are done using the frame data, to release its resources */
using PushDmaBufFrameCallback = std::function<void(const DmaBufFrame& frame, FrameDoneCallback cb)>;

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

	StreamConnectedCallback streamConnected;
	PushMemoryFrameCallback pushMemoryFrame;
	PushDmaBufFrameCallback pushDmaBufFrame;

	friend void streamStateChanged(void*, pw_stream_state, pw_stream_state, const char*) noexcept;
	friend void processFrame(void*) noexcept;

public:
	/** Create a new PipeWire stream that is connected to the given shared video stream.
	 * To actually start streaming and pushing frames to the later stages, you need to call runStreamLoop().
	 * The given callback functions are called for their respective events. See their documentation for information
	 * about how they are used. */
	PipeWireStream(const SharedScreen& shareInfo, StreamConnectedCallback streamConnected,
	               PushMemoryFrameCallback pushMemoryFrameCb,
	               PushDmaBufFrameCallback pushDmaBufFrameCb);

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