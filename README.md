Description
==================
This is the home of libscreencapture-wayland, a modular library for capturing and recording monitor and window content inside a Wayland session of a GNU/Linux desktop.
It aims to be a component to include into applications, so they can easily gain this functionality without doing all the work on their own.

**Currently in early development, work in progress**

Rationale
---------
Recording a traditional X11 desktop is easy with existing tools.
Because every application can grab the window content of any window or monitor on the X11 server, screen sharing is rather simple to do.
Many applications support this method like *ffmpeg* with the *x11grab* source or apps built with the [Electron framework](https://www.electronjs.org/).
The typical APIs for this are *XShm* and *XComposite*, which are only available on X11.

With wayland, this becomes more difficult from a developers point of view.
The security architecture of wayland forbids unauthorized access to other windows.
No capture API is provided by wayland itself.

Instead, applications must explicitly request the user to grant them access to a screen or window.
This is done through [xdg-desktop-portal](https://flatpak.github.io/xdg-desktop-portal/), a D-Bus API that was originally invented for sandboxed Flatpak apps.

The actual window content is delivered through [PipeWire](https://pipewire.org/) in the form of a video stream.
To access it, an application has to connect to this stream and negotiate a format with the stream source.

Finally, the video data typically has to be compressed by encoding it with a video codec like H.264 or VP9.
For performance reasons, this should be done in the process where the video stream arrives, otherwise copying of multiple gigabytes per second of data is necessary.

As of now, no easy-to-use solution for the above steps is available for integration into applications.
To help the adoption of wayland as a successor to X11, this project aims to fill this gap.

Modules
-------
The library is split into modules, so each can be independently used and chosen to the developer's needs.
For example, an application might not need to encode the video data, so it only uses the Portal and PipeWire modules.

###Portal module

###PipeWire module

###FFmpeg module

Building the library
====================

###Dependencies

- C++ 17 support
- `CMake` 3.18+
- `pkg-config` – for finding other dependencies

For the Portal module:
- `libsystemd` v236+ – for the D-Bus client (no systemd required, only this library)

For the PipeWire module:
- `libpipewire` v0.3 – for connecting to the PipeWire daemon and the video stream

For the FFmpeg module:

Either
- `ffmpeg` 5.0 – for encoding the video stream

Or
- `libva`
- `libdrm`
- `zlib`

for building a minimal ffmpeg with VAAPI support.