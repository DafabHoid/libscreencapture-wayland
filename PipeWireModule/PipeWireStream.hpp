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
#include <optional>
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


/** This class encapsulates a receiver of a PipeWire video stream. It connects to the stream, negotiates a suitable
 * frame format and starts receiving frames. Stream events (like when a frame has been received) can be polled for with
 * the pollEvent() method.
 *
 * An example usage of the stream looks like this:
 * @code
 * PipeWireStream pwStream(shareInfo, supportDmaBuf);
 * bool shouldStop = false;
 * while (!shouldStop)
 * {
 *     auto ev = pwStream.pollEvent(std::chrono::seconds(-1));
 *     if (ev)
 *     {
 *         // call lambda function appropriate for the type of *ev
 *         std::visit(overloaded{
 *             [&] (pw::event::Connected& e)
 *             {
 *                 ...
 *             },
 *             [&] (pw::event::Disconnected&)
 *             {
 *                 shouldStop = true;
 *             },
 *             [&] (pw::event::MemoryFrameReceived& e)
 *             {
 *                 ...
 *             },
 *             [&] (pw::event::DmaBufFrameReceived& e)
 *             {
 *                 // might only reach this when you passed supportDmaBuf=true
 *             }
 *         }, *ev);
 *     }
 * }
 * @endcode */
class PipeWireStream
{
	struct StreamInfo
	{
		pw_stream* stream;
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
			uint8_t* bitmap;
		} cursorBitmap;
	};

	pw_main_loop* mainLoop;
	pw_context* ctx;
	pw_core* core;
	StreamInfo streamData;
	spa_hook coreListener;
	std::queue<event::Event> eventQueue;

	friend void streamStateChanged(void*, pw_stream_state, pw_stream_state, const char*) noexcept;
	friend void processFrame(void*) noexcept;
	friend void streamParamChanged(void*, uint32_t, const spa_pod*) noexcept;
	friend void coreError(void*, uint32_t, int, int, const char*) noexcept;

public:
	/** Create a new PipeWire stream that is connected to the given shared video stream.
	 * To actually start streaming and receiving events like @em FrameReceived, you need to call pollEvent() in a loop.
	 * Negotiating DmaBuf-shared (zerocopy) frames is enabled when you set supportDmaBuf to true, but the other side
	 * (the display server) can ignore this request and provide memory-mapped frames.
	 * When supportDmaBuf is false, DmaBufFrameReceived events are never generated.
	 * @param shareInfo PipeWire descriptor of the shared stream to connect to
	 * @param supportDmaBuf Set to true if you want to support DmaBuf frames */
	SCW_EXPORT PipeWireStream(const SharedScreen& shareInfo, bool supportDmaBuf);

	/** Destroy the stream and all frames associated with it.
	 *
	 * If the stream is still running, this will disconnect it.
	 * Make sure that you don't have any references to frames from this stream left when destroying it.*/
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