/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include "common.hpp"
#include "PipeWireStream.hpp"
#include <spa/param/video/format-utils.h>
#include <spa/pod/pod.h>
#include <spa/debug/format.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <stdexcept> // runtime_error
#include <algorithm> // max
#include <libdrm/drm_fourcc.h>
#include <signal.h> // sigaction

namespace pw
{

static volatile bool globalStopFlag = false;

static void setGlobalStopFlag(int)
{
	globalStopFlag = true;
}

void quit(pw::StreamInfo* si, std::exception_ptr ep)
{
	si->mainLoopInfo->setError(ep);
	pw_stream_set_active(si->stream, false);
	pw_stream_flush(si->stream, false);
}

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
	auto si = static_cast<pw::StreamInfo*>(userData);
	if (pw_stream_get_state(si->stream, nullptr) != PW_STREAM_STATE_STREAMING)
		return;
	if (globalStopFlag)
	{
		printf("Stopping stream...\n");
		quit(si, 0);
		return;
	}

	pw_buffer* b = pw_stream_dequeue_buffer(si->stream);
	if (!b) {
		// out of buffers
		return;
	}

	spa_meta_cursor *mcs;
	mcs = static_cast<spa_meta_cursor *>(spa_buffer_find_meta_data(b->buffer, SPA_META_Cursor, sizeof(*mcs)));
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
	try {
		spa_data& d = b->buffer->datas[0];
		if (d.type == SPA_DATA_MemPtr || d.type == SPA_DATA_MemFd) {
			printf("Memory-mapped buffer info: size = %x, stride = %x, ptr = %p\n",
			       d.chunk->size, d.chunk->stride, d.data);
			assert(b->buffer->n_datas == 1);
			MemoryFrame frame{
					.width = si->format.info.raw.size.width,
					.height = si->format.info.raw.size.height,
					.format = spa2pixelFormat(si->format.info.raw.format),
					.memory = d.data,
					.stride = static_cast<size_t>(d.chunk->stride),
					.size = d.chunk->size,
					.offset = d.chunk->offset
			};
			si->mainLoopInfo->pushMemoryFrame(frame, [si, b]() {
				pw_stream_queue_buffer(si->stream, b);
			});
		} else if (d.type == SPA_DATA_DmaBuf) {
			unsigned int planeCount = std::min(b->buffer->n_datas, 4u);
			printf("DMA-BUF info: fd = %ld, size = %x, stride = %x, planeCount = %u, offset = %x\n",
			       d.fd, d.chunk->size, d.chunk->stride, planeCount, d.chunk->offset);
			// sum up all chunk sizes
			size_t totalSize = 0;
			for (unsigned int i = 0; i < planeCount; ++i)
			{
				totalSize += b->buffer->datas[i].chunk->size;
			}
			DmaBufFrame frame{
					.width = si->format.info.raw.size.width,
					.height = si->format.info.raw.size.height,
					.drmFormat = spa2drmFormat(si->format.info.raw.format),
					.drmObject = {
							.fd = static_cast<int>(b->buffer->datas[0].fd),
							.totalSize = totalSize,
							.modifier = static_cast<uint64_t>(si->format.info.raw.modifier),
					},
					.planeCount = planeCount
			};
			for (unsigned int l = 0; l < planeCount; ++l) {
				auto& plane = frame.planes[l];
				spa_chunk& chunk = *b->buffer->datas[l].chunk;
				plane.offset = chunk.offset;
				plane.pitch = chunk.stride;
			}
			si->mainLoopInfo->pushDmaBufFrame(frame, [si, b]() {
				pw_stream_queue_buffer(si->stream, b);
			});
		}
	}
	catch (const std::exception& e)
	{
		// exceptions must not travel outside this function, because it is called by C code
		// → catch them here instead
		quit(si, std::current_exception());
	}
}

void streamStateChanged(void* userData, pw_stream_state old, pw_stream_state nw, const char* msg) noexcept
{
	auto si = static_cast<pw::StreamInfo*>(userData);
	printf("\x1b[1mStream state changed:\x1b[0m old: %s, new: %s, msg: %s\n", pw_stream_state_as_string(old), pw_stream_state_as_string(nw), msg);
	if (old == PW_STREAM_STATE_PAUSED && nw == PW_STREAM_STATE_STREAMING)
	{
		auto& raw = si->format.info.raw;
		try
		{
			si->mainLoopInfo->streamConnected(Rect {raw.size.width, raw.size.height}, spa2pixelFormat(raw.format), si->haveDmaBuf);
		}
		catch (const std::exception& e)
		{
			quit(si, std::current_exception());
		}
	}
	else if (old == PW_STREAM_STATE_STREAMING)
	{
		si->mainLoopInfo->quit();
	}
}

#define CURSOR_META_SIZE(width, height)                                \
	(sizeof(struct spa_meta_cursor) + sizeof(struct spa_meta_bitmap) + \
	 width * height * 4)

static void streamParamChanged(void* userdata, uint32_t paramID, const spa_pod* param) noexcept
{
	if (!param || paramID != SPA_PARAM_Format)
		return;
	auto si = static_cast<pw::StreamInfo*>(userdata);
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
	const spa_pod* params[2];
	params[0] = static_cast<spa_pod*>(spa_pod_builder_add_object(&b,
	                                                             SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
	                                                             SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Cursor),
																 SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(
																		 CURSOR_META_SIZE(24, 24),
																		 CURSOR_META_SIZE(1, 1),
																		 CURSOR_META_SIZE(256, 256)
																 )));
	params[1] = static_cast<spa_pod*>(spa_pod_builder_add_object(&b,
	                                                             SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
																 SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(16),
	                                                             SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(bufferTypes)));
	assert(params[0] && params[1] && params[0]->size + params[1]->size<= sizeof(buffer));
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

static void coreError(void* userData, uint32_t id, int seq, int res, const char* msg) noexcept
{
	auto si = static_cast<pw::StreamInfo*>(userData);
	printf("PipeWire error, id = %u, seq = %d, res = %d (%s): %s\n", id, seq, res, strerror(res), msg);
	quit(si, std::make_exception_ptr(std::runtime_error("PipeWire error")));
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
		spa_pod_builder_long(&b, DRM_FORMAT_MOD_INVALID);
		spa_pod_builder_pop(&b, &f2);
	}
	return static_cast<const spa_pod*>(spa_pod_builder_pop(&b, &f));
}

PipeWireStream::PipeWireStream(const SharedScreen& shareInfo,
                                   StreamConnectedCallback streamConnected,
                                   PushMemoryFrameCallback pushMemoryFrameCb,
                                   PushDmaBufFrameCallback pushDmaBufFrameCb)
: mainLoop{pw_main_loop_new(nullptr)},
  ctx{pw_context_new(pw_main_loop_get_loop(mainLoop), nullptr, 0)},
  core{},
  streamInfo{},
  streamConnected{std::move(streamConnected)},
  pushMemoryFrame{std::move(pushMemoryFrameCb)},
  pushDmaBufFrame{std::move(pushDmaBufFrameCb)}
{
#ifndef NDEBUG
	pw_log_set_level(spa_log_level::SPA_LOG_LEVEL_DEBUG);
#endif

	// first connect to the PipeWire instance given by the shared file descriptor
	core = pw_context_connect_fd(ctx, shareInfo.pipeWireFd, nullptr, 0);
	if (!core)
	{
		throw std::runtime_error("PipeWire connection failed");
	}

	// register callbacks for core info and error events
	streamInfo.mainLoopInfo = this;
	pw_core_add_listener(core, &coreListener, &coreEvents, &streamInfo);

	// create a new video stream with our stream event callbacks
	pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
											 PW_KEY_MEDIA_CATEGORY, "Capture",
											 PW_KEY_MEDIA_ROLE, "Screen", nullptr);
	streamInfo.stream = pw_stream_new_simple(pw_main_loop_get_loop(mainLoop), "GfxTablet ScreenCapture",
												 props, &streamEvents, &streamInfo);
	if (!streamInfo.stream)
	{
		throw std::runtime_error("Could not create stream");
	}

	// build a parameters list for our stream and connect it to the shared PipeWire node ID
	char buffer[0x300];
	spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	const spa_pod* params[2];

	params[0] = buildStreamParams(b, true);
	params[1] = buildStreamParams(b, false);

	assert(params[0] && params[1] && params[0]->size + params[1]->size <= sizeof(buffer));

	if (pw_stream_connect(streamInfo.stream, PW_DIRECTION_INPUT, shareInfo.pipeWireNode,
	                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_AUTOCONNECT),
						  params, sizeof(params)/sizeof(params[0])) < 0)
	{
		throw std::runtime_error("Stream connect failed");
	}

	// register a signal handler for a graceful shutdown
	struct sigaction sigIntHandler {};
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_handler = &setGlobalStopFlag;
	sigaction(SIGINT, &sigIntHandler, nullptr);
	sigaction(SIGTERM, &sigIntHandler, nullptr);
}

PipeWireStream::~PipeWireStream() noexcept
{
	if (streamInfo.stream)
	{
		pw_stream_disconnect(streamInfo.stream);
		pw_stream_destroy(streamInfo.stream);
	}
	if (streamInfo.cursorBitmap.bitmap)
		delete[] streamInfo.cursorBitmap.bitmap;
	if (core) pw_core_disconnect(core);
	if (ctx) pw_context_destroy(ctx);
	if (mainLoop) pw_main_loop_destroy(mainLoop);
}

void PipeWireStream::quit() noexcept
{
	pw_main_loop_quit(mainLoop);
}

void PipeWireStream::setError(std::exception_ptr ep) noexcept
{
	streamException = std::move(ep);
}

void PipeWireStream::runStreamLoop()
{
	pw_main_loop_run(mainLoop);
	// if an exception occurred during the main loop, pass it on
	if (streamException)
		std::rethrow_exception(std::move(streamException));
}

}