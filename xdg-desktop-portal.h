/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_XDG_DESKTOP_PORTAL_H
#define SCREENCAPTURE_XDG_DESKTOP_PORTAL_H

enum CursorMode
{
	CURSOR_MODE_HIDDEN = 1,
	CURSOR_MODE_EMBED = 2,
	CURSOR_MODE_META = 4,
};

#ifdef __cplusplus
#include "common.hpp"

namespace portal
{

/** Request a shared screen via D-Bus from xdg-desktop-portal. This will ask the user if they want
 * to share their screen and which screen to share to this application.
 * The returned SharedScreen object allows you to acquire a PipeWire video stream for the screen.
 * @param cursorMode a bitfield of #CursorMode flags that describe which cursor mode you want to request
 *     (may be ignored by the portal)
 * @return A SharedScreen object on success, or nothing if the user cancelled the request
 * @throw std::exception if an I/O or protocol error occurs */
std::optional<SharedScreen> requestPipeWireShare(CursorMode cursorMode);

}

extern "C"
{
#endif // __cplusplus

// C interface

typedef struct
{
	void* connection;
	int pipeWireFd;
	uint32_t pipeWireNode;
} SharedScreen_t;

SharedScreen_t* requestPipeWireShareFromPortal(enum CursorMode cursorMode);

void dropSharedScreen(SharedScreen_t* shareInfo);


#ifdef __cplusplus
} // extern "C"
#endif

#endif //SCREENCAPTURE_XDG_DESKTOP_PORTAL_H
