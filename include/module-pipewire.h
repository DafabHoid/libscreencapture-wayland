/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_MODULE_PIPEWIRE_H
#define SCREENCAPTURE_MODULE_PIPEWIRE_H

#include "c_common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
	void (*streamConnected)(const struct Rect* dimensions, enum PixelFormat format, bool isDmaBuf);

	void (*streamDisconnected)();

	void (*pushMemoryFrame)(const struct MemoryFrame* frame);

	void (*pushDmaBufFrame)(const struct DmaBufFrame* frame);
} StreamCallbacks_t;

struct PipeWireStream;

SCW_EXPORT struct PipeWireStream* PipeWireStream_connect(const SharedScreen_t* shareInfo, const StreamCallbacks_t* cbs);

SCW_EXPORT void PipeWireStream_free(struct PipeWireStream* stream);

SCW_EXPORT int PipeWireStream_runStreamLoop(struct PipeWireStream* stream);

SCW_EXPORT void PipeWireStream_quit(struct PipeWireStream* stream);

#ifdef __cplusplus
} // extern "C"
#endif


#endif //SCREENCAPTURE_MODULE_PIPEWIRE_H