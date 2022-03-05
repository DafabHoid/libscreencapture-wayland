/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_XDG_DESKTOP_PORTAL_HPP
#define SCREENCAPTURE_XDG_DESKTOP_PORTAL_HPP

#include "common.hpp"
#include <module-portal.h>

namespace portal
{
using common::SharedScreen;

/** Request a shared screen via D-Bus from xdg-desktop-portal. This will ask the user if they want
 * to share their screen and which screen to share to this application.
 * The returned SharedScreen object allows you to acquire a PipeWire video stream for the screen.
 * @param cursorMode a bitfield of #CursorMode flags that describe which cursor mode you want to request
 *     (may be ignored by the portal)
 * @return A SharedScreen object on success, or nothing if the user cancelled the request
 * @throw std::exception if an I/O or protocol error occurs */
SCW_EXPORT std::optional<SharedScreen> requestPipeWireShare(CursorMode cursorMode);

}

#endif //SCREENCAPTURE_XDG_DESKTOP_PORTAL_HPP
