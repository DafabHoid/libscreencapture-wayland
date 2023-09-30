/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_C_COMMON_H
#define SCREENCAPTURE_C_COMMON_H

#include <stdint.h>
#include <stddef.h> // size_t
#include <stdlib.h> // free()

#ifndef SCW_EXPORT
#define SCW_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

SCW_EXPORT void screencapture_wayland_init(int* argc, char*** argv);
SCW_EXPORT void screencapture_wayland_deinit();

#ifdef __cplusplus
}
#endif

typedef struct
{
	void* connection;
	int pipeWireFd;
	uint32_t pipeWireNode;
} SharedScreen_t;

struct Rect
{
	unsigned int w;
	unsigned int h;
};

enum PixelFormat
{
	BGRA,
	BGRX,
	RGBA,
	RGBX,
};

typedef void (*FrameDoneCallback_t)(void*);

struct MemoryFrame
{
	uint32_t width;
	uint32_t height;
	enum PixelFormat format;
	void* memory;
	size_t stride;
	size_t size;
	size_t offset;

	void* opaque;
	FrameDoneCallback_t onFrameDone;
};

__inline void freeMemoryFrame(struct MemoryFrame* frame)
{
	if (frame->onFrameDone)
		frame->onFrameDone(frame->opaque);
	free(frame);
}

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

	void* opaque;
	FrameDoneCallback_t onFrameDone;
};

__inline void freeDmaBufFrame(struct DmaBufFrame* frame)
{
	if (frame->onFrameDone)
		frame->onFrameDone(frame->opaque);
	free(frame);
}




#endif //SCREENCAPTURE_C_COMMON_H