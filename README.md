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

### Portal module
To get access to the content of a window or screen, an application has to ask the user for authorization first.
This is done by invoking functions on the D-Bus interface `org.freedesktop.portal.ScreenCast`, provided by *xdg-desktop-portal* (documented [here](https://flatpak.github.io/xdg-desktop-portal/#gdbus-org.freedesktop.portal.ScreenCast)).
The portal API gives a standard interface between applications and the desktop environment.
The ScreenCast interface is implemented by the compositor.

The portal module implements a client that invokes this interface and returns a handle to a PipeWire stream.

### PipeWire module
When the user accepts a request to share the screen, a PipeWire stream is given to the application.
The wayland compositor streams the screen content through PipeWire to the application in the form of uncompressed images.

The PipeWire module implements a client that connects to a given PipeWire video stream.
It then negotiates the format, which typically is BGRA with 32 bit per channel.
The image data is either provided as a memory-mapped array of pixels, or as a *DmaBuf* file descriptor.
The latter is a handle to a piece of video memory (implemented with [DRM PRIME buffer sharing](https://www.kernel.org/doc/html/latest/gpu/drm-mm.html#prime-buffer-sharing)).
This can lead to better performance, because the screen's frame buffer does not have to be copied to system memory.
Especially when encoding the frames on the GPU later on, this gives better performance and less energy consumption.

### FFmpeg module
The FFmpeg module implements a video encoder through the [ffmpeg](https://ffmpeg.org/) libraries.
Uncompressed frame data that is passed in is scaled, encoded and muxed to a container format.
It uses GPU encoding with VAAPI (supported on Intel and AMD graphic cards).

#### Stages
1. Hardware upload: Each frame is copied to the GPU, or (in the case of DmaBuf) directly used.
2. Scaling and format conversion: A `scale_vaapi` filter scales each frame on the GPU to the target size, converting it to the NV12 pixel format in the process.
   This is the only format that can be encoded by VAAPI.
3. Encoding: The `h264_vaapi` encoder compresses the frames to a H.264 video stream.
4. Muxing: The video stream is packed into a container format and written to a file or sent over the network.

#### Why don't you use ffmpeg directly?
Unfortunately the video frames can't be passed to ffmpeg directly, because ffmpeg would be running as a separate process.
It does not support PipeWire either, so you can't forward the PipeWire stream to ffmpeg.
This means that all frames have to be copied to a socket, pipe or file.
ffmpeg itself would then copy the data from there into the memory, and potentially again to video memory.
This is highly inefficient and slow.

So the ffmpeg code has to run in the same process as the PipeWire module, meaning that the various libav libraries must be used directly.

Building the library
====================

### Dependencies

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