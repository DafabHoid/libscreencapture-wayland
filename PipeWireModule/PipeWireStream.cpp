/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "PipeWireStream.hpp"
#include <spa/param/video/format-utils.h>
#include <spa/pod/pod.h>
#include <spa/debug/format.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cerrno>
#include <stdexcept> // runtime_error
#include <algorithm> // max
#include <libdrm/drm_fourcc.h>
#include <sys/eventfd.h>
#include <unistd.h> // read, write

using namespace std::chrono;

namespace pw
{

static uint32_t spa2drmFormat(spa_video_format format)
{
	switch (format) {
	case SPA_VIDEO_FORMAT_BGRA:
		return DRM_FORMAT_ARGB8888;
	case SPA_VIDEO_FORMAT_BGRx:
		return DRM_FORMAT_XRGB8888;
	case SPA_VIDEO_FORMAT_RGBA:
		return DRM_FORMAT_ABGR8888;
	case SPA_VIDEO_FORMAT_RGBx:
		return DRM_FORMAT_XBGR8888;
	default:
		throw std::runtime_error("invalid format");
	}
}

static constexpr inline PixelFormat spa2pixelFormat(spa_video_format format)
{
	switch (format)
	{
		case SPA_VIDEO_FORMAT_RGBA:
			return PixelFormat::RGBA;
		case SPA_VIDEO_FORMAT_RGBx:
			return PixelFormat::RGBX;
		case SPA_VIDEO_FORMAT_BGRA:
			return PixelFormat::BGRA;
		case SPA_VIDEO_FORMAT_BGRx:
			return PixelFormat::BGRX;
		default:
			throw std::runtime_error("unsupported spa video format");
	}
}


void processFrame(void* userData) noexcept
{
	auto pwStream = static_cast<pw::PipeWireStream*>(userData);
	auto si = &pwStream->streamData;
	if (pw_stream_get_state(si->stream, nullptr) != PW_STREAM_STATE_STREAMING)
		return;

	pw_buffer* b = pw_stream_dequeue_buffer(si->stream);
	if (!b) {
		// out of buffers
		return;
	}

	spa_meta_cursor *mcs;
	mcs = static_cast<spa_meta_cursor*>(spa_buffer_find_meta_data(b->buffer, SPA_META_Cursor, sizeof(*mcs)));
	if (mcs && spa_meta_cursor_is_valid(mcs))
	{
		si->cursorPos.x = mcs->position.x;
		si->cursorPos.y = mcs->position.y;
		if (mcs->bitmap_offset >= sizeof(*mcs))
		{
			spa_meta_bitmap* mb = SPA_MEMBER(mcs, mcs->bitmap_offset, struct spa_meta_bitmap);
			si->cursorBitmap.w = mb->size.width;
			si->cursorBitmap.h = mb->size.height;
			const uint8_t* bitmap = SPA_MEMBER(mb, mb->offset, uint8_t);
			size_t size = si->cursorBitmap.w * si->cursorBitmap.h * 4;
			if (si->cursorBitmap.bitmap)
				delete[] si->cursorBitmap.bitmap;
			si->cursorBitmap.bitmap = new uint8_t[size];
			std::memcpy(si->cursorBitmap.bitmap, bitmap, size);
			printf("Cursor: (%d,%d) [%d,%d] %s\n", si->cursorPos.x, si->cursorPos.y,
					si->cursorBitmap.w, si->cursorBitmap.h, spa_debug_type_find_name(spa_type_video_format, mb->format));
		}
	}

	nanoseconds pts;
	spa_meta_header* header;
	header = static_cast<spa_meta_header*>(spa_buffer_find_meta_data(b->buffer, SPA_META_Header, sizeof(*header)));
	if (header)
	{
		pts = nanoseconds(header->pts);
	}
	else
	{
		auto now = steady_clock::now();
		pts = now - si->startTime;
	}

	spa_data& d = b->buffer->datas[0];
	if (d.type == SPA_DATA_MemPtr || d.type == SPA_DATA_MemFd)
	{
#ifndef NDEBUG
		printf("Memory-mapped buffer info: size = %x, stride = %x, ptr = %p\n",
		       d.chunk->size, d.chunk->stride, d.data);
#endif
		assert(b->buffer->n_datas == 1);
		auto f = std::make_unique<MemoryFrame>();
		f->width = si->format.info.raw.size.width;
		f->height = si->format.info.raw.size.height;
		f->pts = pts;
		f->format = spa2pixelFormat(si->format.info.raw.format);
		f->memory = d.data;
		f->stride = static_cast<size_t>(d.chunk->stride);
		f->size = d.chunk->size;
		f->offset = d.chunk->offset;
		f->onFrameDone = [si, b]()
		{
			pw_stream_queue_buffer(si->stream, b);
		};
		pwStream->enqueueEvent(event::MemoryFrameReceived{std::move(f)});
	}
	else if (d.type == SPA_DATA_DmaBuf)
	{
		// No DRM format uses more than 4 planes, so ignore higher values (DmaBufFrame only supports 4 planes)
		unsigned int planeCount = std::min(b->buffer->n_datas, 4u);
#ifndef NDEBUG
		printf("DMA-BUF info: fd = %ld, size = %x, totalSize = %x, stride = %x, planeCount = %u, offset = %x\n",
		       d.fd, d.chunk->size, d.maxsize, d.chunk->stride, b->buffer->n_datas, d.chunk->offset);
#endif

		auto f = std::make_unique<DmaBufFrame>();
		f->width = si->format.info.raw.size.width;
		f->height = si->format.info.raw.size.height;
		f->pts = pts;
		f->drmFormat = spa2drmFormat(si->format.info.raw.format);
		f->drmObject = {
				.fd = static_cast<int>(b->buffer->datas[0].fd),
				.totalSize = b->buffer->datas[0].maxsize,
				.modifier = si->format.info.raw.modifier,
		};
		f->planeCount = planeCount;
		f->onFrameDone = [si, b]()
		{
			pw_stream_queue_buffer(si->stream, b);
		};
		for (unsigned int l = 0; l < planeCount; ++l)
		{
			auto& plane = f->planes[l];
			spa_chunk& chunk = *b->buffer->datas[l].chunk;
			plane.offset = chunk.offset;
			plane.pitch = chunk.stride;
		}
		pwStream->enqueueEvent(event::DmaBufFrameReceived{std::move(f)});
	}
}

void streamStateChanged(void* userData, pw_stream_state old, pw_stream_state nw, const char* msg) noexcept
{
	auto pwStream = static_cast<pw::PipeWireStream*>(userData);
	pwStream->streamData.state = nw;
	printf("\x1b[1mStream state changed:\x1b[0m old: %s, new: %s, msg: %s\n", pw_stream_state_as_string(old), pw_stream_state_as_string(nw), msg);
	if (old == PW_STREAM_STATE_PAUSED && nw == PW_STREAM_STATE_STREAMING)
	{
		auto& raw = pwStream->streamData.format.info.raw;
		pwStream->enqueueEvent(event::Connected {
			Rect {raw.size.width, raw.size.height},
			spa2pixelFormat(raw.format),
			pwStream->streamData.haveDmaBuf
		});
		pwStream->streamData.startTime = steady_clock::now();
	}
	else if (old == PW_STREAM_STATE_STREAMING)
	{
		pwStream->enqueueEvent(event::Disconnected{});
	}
}

#define CURSOR_META_SIZE(width, height)                                \
	(sizeof(struct spa_meta_cursor) + sizeof(struct spa_meta_bitmap) + \
	 width * height * 4)

void streamParamChanged(void* userdata, uint32_t paramID, const spa_pod* param) noexcept
{
	if (!param || paramID != SPA_PARAM_Format)
		return;
	auto pwStream = static_cast<pw::PipeWireStream*>(userdata);
	auto si = &pwStream->streamData;
	if (spa_format_parse(param, &si->format.media_type, &si->format.media_subtype) < 0)
		return;
	if (spa_format_video_raw_parse(param, &si->format.info.raw) < 0)
		return;

	const spa_pod_prop* modifier = spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier);
	si->haveDmaBuf = modifier != nullptr;

	auto& formatInfo = si->format.info.raw;
	printf("Video format:\n\t%s (%#x)\n\tsize = %ux%u\n\tframerate = %d/%d\n\tmodifier = %#lx\n",
			spa_debug_type_find_name(spa_type_video_format, formatInfo.format), formatInfo.format,
			formatInfo.size.width, formatInfo.size.height,
			formatInfo.framerate.num, formatInfo.framerate.denom,
			formatInfo.modifier);


	char buffer[0x100];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	uint32_t bufferTypes = (1 << SPA_DATA_MemPtr);
	if (modifier)
		bufferTypes |= (1 << SPA_DATA_DmaBuf);
	const spa_pod* params[3];
	params[0] = static_cast<spa_pod*>(spa_pod_builder_add_object(&b,
			SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
			SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Cursor),
			SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(
					CURSOR_META_SIZE(24, 24),
					CURSOR_META_SIZE(1, 1),
					CURSOR_META_SIZE(256, 256)
			)
	));
	params[1] = static_cast<spa_pod*>(spa_pod_builder_add_object(&b,
			SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
			SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
			SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header))
	));
	params[2] = static_cast<spa_pod*>(spa_pod_builder_add_object(&b,
			SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
			SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(16),
			SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(bufferTypes)
	));
	assert(params[0] && params[1] && params[2] && params[0]->size + params[1]->size + params[2]->size <= sizeof(buffer));

	pw_stream_update_params(si->stream, params, sizeof(params)/sizeof(params[0]));
}


static const pw_stream_events streamEvents = {
		.version = PW_VERSION_STREAM_EVENTS,
		.state_changed = streamStateChanged,
		.param_changed = streamParamChanged,
		.process = processFrame,
};

static void coreInfo(void* userData, const pw_core_info* info) noexcept
{
	printf("PipeWire Info: version %s, connection name: %s, user %s on %s\n", info->version, info->name, info->user_name, info->host_name);
}

void coreError(void* userData, uint32_t id, int seq, int res, const char* msg) noexcept
{
	auto pwStream = static_cast<pw::PipeWireStream*>(userData);
	fprintf(stderr, "PipeWire error, id = %u, seq = %d, res = %d (%s): %s\n", id, seq, res, strerror(res), msg);
	pw_stream_set_active(pwStream->streamData.stream, false);
	pw_stream_flush(pwStream->streamData.stream, false);
}

static const pw_core_events coreEvents = {
		.version = PW_VERSION_CORE_EVENTS,
		.info = coreInfo,
		.error = coreError,
};

static const spa_pod* buildStreamParams(spa_pod_builder& b, bool withDMABuf)
{
	auto sizeDefault = SPA_RECTANGLE(1280, 720);
	auto sizeMin = SPA_RECTANGLE(1, 1);
	auto sizeMax = SPA_RECTANGLE(4096, 4096);
	auto rateDefault = SPA_FRACTION(30, 1);
	auto rateMin = SPA_FRACTION(0, 1);
	auto rateMax = SPA_FRACTION(240, 1);
	spa_pod_frame f, f2;
	spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
	spa_pod_builder_add(&b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
	spa_pod_builder_add(&b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
	spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_format,
	                    SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_NV12,
	                                           SPA_VIDEO_FORMAT_RGBx,
	                                           SPA_VIDEO_FORMAT_BGRx,
	                                           SPA_VIDEO_FORMAT_BGRA,
	                                           SPA_VIDEO_FORMAT_RGBA), 0);
	spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_size,      SPA_POD_CHOICE_RANGE_Rectangle(&sizeDefault, &sizeMin, &sizeMax), 0);
	spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&rateDefault, &rateMin, &rateMax), 0);
	if (withDMABuf)
	{
		spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_modifier,
		                     SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
		spa_pod_builder_push_choice(&b, &f2, SPA_CHOICE_Enum, 0);
		spa_pod_builder_long(&b, DRM_FORMAT_MOD_LINEAR);
		spa_pod_builder_long(&b, I915_FORMAT_MOD_X_TILED);
		spa_pod_builder_long(&b, I915_FORMAT_MOD_Y_TILED);
		spa_pod_builder_long(&b, I915_FORMAT_MOD_Yf_TILED);
		spa_pod_builder_long(&b, I915_FORMAT_MOD_Y_TILED_CCS);
		spa_pod_builder_long(&b, I915_FORMAT_MOD_Yf_TILED_CCS);

		spa_pod_builder_long(&b, AMD_FMT_MOD|AMD_FMT_MOD_SET(TILE_VERSION, 0));
		spa_pod_builder_long(&b, AMD_FMT_MOD
		                         | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX9)
								 | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S));
		spa_pod_builder_long(&b, AMD_FMT_MOD
		                         | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10)
		                         | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_S));
		spa_pod_builder_long(&b, AMD_FMT_MOD
		                         | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS)
		                         | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X)
		                         | AMD_FMT_MOD_SET(PIPE_XOR_BITS, 4)
		                         | AMD_FMT_MOD_SET(PACKERS, 3));
		spa_pod_builder_long(&b, AMD_FMT_MOD
		                         | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS)
		                         | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X)
		                         | AMD_FMT_MOD_SET(DCC, 1)
		                         | AMD_FMT_MOD_SET(DCC_RETILE, 1)
		                         | AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1)
		                         | AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_128B)
		                         | AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1)
								 | AMD_FMT_MOD_SET(PIPE_XOR_BITS, 4)
								 | AMD_FMT_MOD_SET(PACKERS, 3));
		spa_pod_builder_long(&b, AMD_FMT_MOD
		                         | AMD_FMT_MOD_SET(TILE_VERSION, AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS)
		                         | AMD_FMT_MOD_SET(TILE, AMD_FMT_MOD_TILE_GFX9_64K_R_X)
		                         | AMD_FMT_MOD_SET(DCC, 1)
		                         | AMD_FMT_MOD_SET(DCC_RETILE, 0)
		                         | AMD_FMT_MOD_SET(DCC_INDEPENDENT_128B, 1)
		                         | AMD_FMT_MOD_SET(DCC_MAX_COMPRESSED_BLOCK, AMD_FMT_MOD_DCC_BLOCK_128B)
		                         | AMD_FMT_MOD_SET(DCC_CONSTANT_ENCODE, 1)
		                         | AMD_FMT_MOD_SET(PIPE_XOR_BITS, 4)
		                         | AMD_FMT_MOD_SET(PACKERS, 3));
		spa_pod_builder_long(&b, DRM_FORMAT_MOD_INVALID);
		spa_pod_builder_pop(&b, &f2);
	}
	return static_cast<const spa_pod*>(spa_pod_builder_pop(&b, &f));
}

PipeWireStream::PipeWireStream(const SharedScreen& shareInfo, bool supportDmaBuf)
: mainLoop{pw_main_loop_new(nullptr)},
  ctx{pw_context_new(pw_main_loop_get_loop(mainLoop), nullptr, 0)},
  core{},
  streamData{},
  eventFd{-1}
{
#ifndef NDEBUG
	pw_log_set_level(spa_log_level::SPA_LOG_LEVEL_DEBUG);
#endif

	eventFd = eventfd(0, EFD_CLOEXEC);
	if (eventFd == -1)
	{
		throw std::runtime_error(std::string("eventfd creation failed") + strerror(errno));
	}

	// first connect to the PipeWire instance given by the shared file descriptor
	core = pw_context_connect_fd(ctx, shareInfo.pipeWireFd, nullptr, 0);
	if (!core)
	{
		throw std::runtime_error("PipeWire connection failed");
	}

	// register callbacks for core info and error events
	pw_core_add_listener(core, &coreListener, &coreEvents, this);

	// create a new video stream with our stream event callbacks
	pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
											 PW_KEY_MEDIA_CATEGORY, "Capture",
											 PW_KEY_MEDIA_ROLE, "Screen", nullptr);
	streamData.stream = pw_stream_new_simple(pw_main_loop_get_loop(mainLoop), "GfxTablet ScreenCapture",
	                                         props, &streamEvents, this);
	if (!streamData.stream)
	{
		throw std::runtime_error("Could not create stream");
	}

	// build a parameters list for our stream and connect it to the shared PipeWire node ID
	char buffer[0x300];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	const spa_pod* params[2];

	params[0] = buildStreamParams(b, supportDmaBuf);
	params[1] = buildStreamParams(b, false);

	assert(params[0] && params[1] && params[0]->size + params[1]->size <= sizeof(buffer));

	if (pw_stream_connect(streamData.stream, PW_DIRECTION_INPUT, shareInfo.pipeWireNode,
	                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_AUTOCONNECT),
	                      params, sizeof(params)/sizeof(params[0])) < 0)
	{
		throw std::runtime_error("Stream connect failed");
	}

	mainLoopThread = std::thread([mainLoop = this->mainLoop]() {
		pw_main_loop_run(mainLoop);
	});
}

PipeWireStream::~PipeWireStream() noexcept
{
	if (mainLoop)
	{
		if (streamData.stream)
		{
			// invoke function inside main loop to disable the stream, so no more events are generated
			auto f = [](spa_loop*, bool, uint32_t, const void*, size_t, void* userData)
			{
				auto stream = static_cast<pw_stream*>(userData);
				return pw_stream_set_active(stream, false);
			};
			pw_loop_invoke(pw_main_loop_get_loop(mainLoop), f, 0, nullptr, 0, false, streamData.stream);
		}
		// quit main loop so the mainLoopThread can terminate
		pw_main_loop_quit(mainLoop);
	}
	// wait for main loop termination to make sure *this is not used concurrently afterward
	if (mainLoopThread.joinable())
		mainLoopThread.join();
	// clear the event queue before destroying the stream or anything else, as the events might still reference it
	while(!eventQueue.empty())
		eventQueue.pop();
	if (streamData.stream)
	{
		pw_stream_disconnect(streamData.stream);
		pw_stream_destroy(streamData.stream);
	}
	if (eventFd != -1)
		close(eventFd);
	if (streamData.cursorBitmap.bitmap)
		delete[] streamData.cursorBitmap.bitmap;
	if (core) pw_core_disconnect(core);
	if (ctx) pw_context_destroy(ctx);
	if (mainLoop) pw_main_loop_destroy(mainLoop);
}

std::optional<pw::event::Event> PipeWireStream::nextEvent()
{
	if (streamData.state == PW_STREAM_STATE_UNCONNECTED)
	{
		[[unlikely]]
		throw std::runtime_error("PipeWireStream::pollEvent called on a disconnected stream");
	}
	if (streamData.state == PW_STREAM_STATE_ERROR)
	{
		const char* error = "Unknown stream error";
		pw_stream_get_state(streamData.stream, &error);
		throw std::runtime_error(std::string("PipeWireStream::pollEvent called, but stream is in failed state. Reason: ") + error);
	}
	std::lock_guard lock(eventQueueMutex);
	if (!eventQueue.empty())
	{
		auto event = std::move(eventQueue.front());
		eventQueue.pop();
		if (eventQueue.empty())
		{
			// clear eventfd status
			char buf[8];
			read(eventFd, buf, sizeof(buf));
		}
		return event;
	}
	return std::nullopt;
}

void PipeWireStream::enqueueEvent(pw::event::Event e) noexcept
{
	std::lock_guard lock(eventQueueMutex);
	eventQueue.push(std::move(e));
	uint64_t num = 1;
	write(eventFd, &num, sizeof(num));
}

int PipeWireStream::getEventPollFd() noexcept
{
	return eventFd;
}

}


// C interface
#include <module-pipewire.h>

static constexpr inline PixelFormat toCFormat(pw::PixelFormat format)
{
	switch (format)
	{
		case pw::PixelFormat::BGRA:
			return PixelFormat::BGRA;
		case pw::PixelFormat::RGBA:
			return PixelFormat::RGBA;
		case pw::PixelFormat::BGRX:
			return PixelFormat::BGRX;
		case pw::PixelFormat::RGBX:
			return PixelFormat::RGBX;
	}
}

class EventToCEventConverter
{
	PipeWireStream_Event* output_c_event;

public:

	void processEvent(pw::event::Event e, PipeWireStream_Event* output_event)
	{
		output_c_event = output_event;
		std::visit(*this, e);
	}

	void operator()(pw::event::Connected& e)
	{
		::Rect c_dimensions = {
				e.dimensions.w,
				e.dimensions.h
		};
		::PixelFormat c_format = toCFormat(e.format);
		output_c_event->type = PWSTREAM_EVENT_TYPE_CONNECTED;
		output_c_event->connect = {c_dimensions, c_format, e.isDmaBuf};
	}

	void operator()(pw::event::Disconnected& e)
	{
		output_c_event->type = PWSTREAM_EVENT_TYPE_DISCONNECTED;
		output_c_event->disconnect = {};
	}

	static void onMemoryFrameDoneWrapper(void* opaque) noexcept
	{
		auto cb = static_cast<pw::MemoryFrame*>(opaque);
		delete cb;
	}

	static void onDmaBufFrameDoneWrapper(void* opaque) noexcept
	{
		auto cb = static_cast<pw::DmaBufFrame*>(opaque);
		delete cb;
	}

	void operator()(pw::event::MemoryFrameReceived& e)
	{
		std::unique_ptr<common::MemoryFrame>& frame = e.frame;
		auto c_frame = static_cast<::MemoryFrame*>(calloc(1, sizeof(::MemoryFrame)));
		c_frame->width = frame->width;
		c_frame->height = frame->height;
		c_frame->format = toCFormat(frame->format);
		c_frame->memory = frame->memory;
		c_frame->size = frame->size;
		c_frame->stride = frame->stride;
		c_frame->offset = frame->offset;

		c_frame->opaque = frame.release();
		c_frame->onFrameDone = &onMemoryFrameDoneWrapper;

		output_c_event->type = PWSTREAM_EVENT_TYPE_MEMORY_FRAME_RECEIVED;
		output_c_event->memoryFrameReceived = {c_frame};
	}

	void operator()(pw::event::DmaBufFrameReceived& e)
	{
		std::unique_ptr<common::DmaBufFrame>& frame = e.frame;
		auto c_frame = static_cast<::DmaBufFrame*>(calloc(1, sizeof(::DmaBufFrame)));
		c_frame->width = frame->width;
		c_frame->height = frame->height;
		c_frame->drmFormat = frame->drmFormat;
		memcpy(&c_frame->drmObject, &frame->drmObject, sizeof(frame->drmObject));
		c_frame->planeCount = frame->planeCount;
		memcpy(c_frame->planes, frame->planes, sizeof(frame->planes));

		c_frame->opaque = frame.release();
		c_frame->onFrameDone = &onDmaBufFrameDoneWrapper;

		output_c_event->type = PWSTREAM_EVENT_TYPE_DMA_BUF_RECEIVED;
		output_c_event->dmaBufFrameReceived = {c_frame};
	}
};

struct PipeWireStream
{
	std::unique_ptr<pw::PipeWireStream> cppStream;
};

struct PipeWireStream* PipeWireStream_connect(const SharedScreen_t* c_shareInfo)
{
	try
	{
		pw::SharedScreen shareInfo = {
				nullptr,
				c_shareInfo->pipeWireFd,
				c_shareInfo->pipeWireNode
		};
		return new PipeWireStream {
			std::make_unique<pw::PipeWireStream>(shareInfo, true)
		};
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "%s\n", e.what());
		return nullptr;
	}
}

void PipeWireStream_free(struct PipeWireStream* stream)
{
	delete stream;
}

int PipeWireStream_getEventPollFd(struct PipeWireStream* stream)
{
	return stream->cppStream->getEventPollFd();
}

int PipeWireStream_nextEvent(struct PipeWireStream* stream, struct PipeWireStream_Event* c_e)
{
	try
	{
		auto event = stream->cppStream->nextEvent();
		if (event)
		{
			EventToCEventConverter().processEvent(std::move(*event), c_e);
			return 1;
		}
		else
		{
			return 0;
		}
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "%s\n", e.what());
		return -1;
	}
}