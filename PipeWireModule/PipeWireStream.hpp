/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_PIPEWIRESTREAM_HPP
#define SCREENCAPTURE_PIPEWIRESTREAM_HPP

#include "../common.hpp"
#include <cstdint>
#include <chrono>
#include <variant>
#include <queue>
#include <pipewire/pipewire.h>
#include <spa/param/video/format.h>

namespace pw
{
using common::Rect;
using common::PixelFormat;
using common::MemoryFrame;
using common::DmaBufFrame;
using common::SharedScreen;

namespace event
{

/** This event is the first you will receive once the PipeWire stream became connected.
 * You should use it to set up a consumer that processes the frames given to you by
 * the MemoryFrameReceived and DmaBufFrameReceived events.
 *
 * A stream can either provide the frames in conventional memory or as a reference to GPU memory.
 * The @em isDmaBuf parameter tells you what type it is. The appropriate FrameReceived is sent
 * for each type. */
class Connected
{
public:
	/** The width and height of the stream in pixels */
	Rect dimensions;

	/** The pixel format of each video frame */
	PixelFormat format;

	/** true if the stream provides DmaBufFrame, false for MemoryFrame */
	bool isDmaBuf;
};

/** This event is sent when the stream is disconnected.
 * You must then stop all ongoing processing of frames and release all frames that were
 * previously given to you by a FrameReceived event, before you finish processing this event. */
class Disconnected {};

/** This event is sent for every frame in the PipeWire stream, if the stream uses
 * conventional memory. Remember to keep the unique_ptr to this frame while using its pixel data. */
class MemoryFrameReceived
{
public:
	/** the frame data including its dimensions, format and a pointer to pixel data */
	std::unique_ptr<MemoryFrame> frame;
};

/** This event is sent for every frame in the PipeWire stream, if the stream uses
 * DmaBuf (GPU) memory. Remember to keep the unique_ptr to this frame while using its file descriptor. */
class DmaBufFrameReceived
{
public:
	/** the frame data including its dimensions, format and a DRM PRIME file descriptor */
	std::unique_ptr<DmaBufFrame> frame;
};


using Event = std::variant<Connected, Disconnected, MemoryFrameReceived, DmaBufFrameReceived>;

} // namespace event



class PipeWireStream;

struct StreamInfo
{
	pw_stream *stream;
	spa_video_info format;
	bool haveDmaBuf;
	pw_stream_state state;
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
	std::queue<event::Event> eventQueue;

	friend void streamStateChanged(void*, pw_stream_state, pw_stream_state, const char*) noexcept;
	friend void processFrame(void*) noexcept;

public:
	/** Create a new PipeWire stream that is connected to the given shared video stream.
	 * To actually start streaming and receiving events like @em FrameReceived, you need to call pollEvent() in a loop. */
	SCW_EXPORT PipeWireStream(const SharedScreen& shareInfo, bool supportDmaBuf);

	SCW_EXPORT ~PipeWireStream() noexcept;

	/** Poll for an event happening in this stream and return it.
	 *
	 * With a non-zero timeout, this function will block until something happens to the PipeWire stream or the timeout
	 * expires, but this does not guarantee that an event is generated (like when the stream state changes from
	 * @em unconnected to @em connecting.
	 * After a pw::event::Disconnected event was returned, this stream is no longer valid and calling this method again
	 * will result in an error.
	 * @param timeout Time to block and wait for new events. Zero means do not block, -1 means wait indefinitely
	 * @throw std::exception In case you called this method again after it returned a disconnected event */
	SCW_EXPORT std::optional<pw::event::Event> pollEvent(std::chrono::seconds timeout);
};

} // namespace pw

#endif //SCREENCAPTURE_PIPEWIRESTREAM_HPP