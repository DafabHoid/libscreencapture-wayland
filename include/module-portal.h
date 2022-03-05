/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_MODULE_PORTAL_H
#define SCREENCAPTURE_MODULE_PORTAL_H

#include "c_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

enum CursorMode
{
	CURSOR_MODE_HIDDEN = 1,
	CURSOR_MODE_EMBED = 2,
	CURSOR_MODE_META = 4,
};

/** Request a shared screen via D-Bus from xdg-desktop-portal. This will ask the user if they want
 * to share their screen and which screen to share to this application.
 * The returned SharedScreen_t object allows you to acquire a PipeWire video stream for the screen.
 * Call dropSharedScreen(SharedScreen_t*) when you want to revoke your request.
 * @param cursorMode a bitfield of #CursorMode flags that describe which cursor mode you want to request
 *     (may be ignored by the portal)
 * @return A SharedScreen object on success, or NULL if the user cancelled the request or a protocol error occurred */
SCW_EXPORT SharedScreen_t* requestPipeWireShareFromPortal(enum CursorMode cursorMode);

/** Drop the SharedScreen_t object and close the D-Bus connection.
 * The screen share permission is revoked and the PipeWire stream closed when doing this.
 * @param shareInfo the SharedScreen_t object you want to drop */
SCW_EXPORT void dropSharedScreen(SharedScreen_t* shareInfo);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //SCREENCAPTURE_MODULE_PORTAL_H
