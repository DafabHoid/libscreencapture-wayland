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

#### Why don't you just use the ffmpeg application?
Unfortunately the video frames can't be passed to ffmpeg directly, because ffmpeg would be running as a separate process.
It does not support PipeWire either, so you can't forward the PipeWire stream to ffmpeg.
This means that all frames have to be copied to a socket, pipe or file.
ffmpeg itself would then copy the data from there into the memory, and potentially again to video memory.
This is highly inefficient and slow.

So the ffmpeg code has to run in the same process as the PipeWire module, meaning that the various libav libraries must be used directly.

### GStreamer module
The GStreamer module implements a video encoder by using the [GStreamer framework](https://gstreamer.freedesktop.org/).
It is an alternative to the FFmpeg module and can sometimes offer better encoding performance.
It performs the same steps as the FFmpeg module to encode the video frames,
but does so by creating a GStreamer pipeline with stages very similar to the stages seen in the FFmpeg section of this document.

One exception is that GStreamer currently does not offer a way to pass DmaBuf frames to the encoding pipeline, so in the hardware upload step a copy is always necessary.

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

For the GStreamer module:

- `gstreamer-base`
- `gstreamer-app` – To feed the video stream into GStreamer
- `gstreamer-video` – For video formats
- `gstreamer-vaapi` – For hardware accelerated video encoding

### Steps to build the example `screencapture` binary
Building is done like a typical CMake project:

    $ mkdir build && cd build
    $ cmake -DCMAKE_BUILD_TYPE=Release ..
    $ cmake --build . --parallel

Some options you can pass vida `-D<option>=<value>` to cmake:
   - `ENABLE_PORTAL_MODULE` Set to OFF to disable the portal module build (default ON)
   - `ENABLE_PIPEWIRE_MODULE` Set to OFF to disable the pipewire module build (default ON)
   - `ENABLE_FFMPEG_MODULE` Set to OFF to disable the ffmpeg module build (default ON)
   - `ENABLE_GSTREAMER_MODULE` Set to ON to enable the gstreamer module build (default OFF)
   - `EXPORT_CPP_INTERFACE` Set to OFF to disable exporting the C++ header files to users of the library and only export the C headers (default ON).
      Because the C++ headers of each module pull in the headers from their dependencies, disabling this option can reduce the amount of include paths.  
   - `BUILD_SHARED_LIBS` Set to ON to build a shared library (default OFF)
   - `BUILD_FFMPEG` Set to ON to build a local minimal ffmpeg distribution for the ffmpeg module.
      Otherwise the system-provided ones are used. (default OFF)

### Using this library inside another CMake project
To use this library as a dependency inside another CMake project, place it inside your existing source directory.

    $ git checkout https://github.com/DafabHoid/libscreencapture-wayland.git

Then, in your `CMakeLists.txt` file, add it as a subdirectory, while setting any options beforehand:

    set(BUILD_SHARED_LIBS ON)
    set(ENABLE_PORTAL_MODULE ON)
    set(ENABLE_PIPEWIRE_MODULE ON)
    set(ENABLE_FFMPEG_MODULE OFF)
    set(ENABLE_GSTREAMER_MODULE OFF)
    set(EXPORT_CPP_INTERFACE OFF)
    add_subdirectory(libscreencapture-wayland)
    
    target_link_libraries(<target> PRIVATE screencapture-wayland)

For available options see the section above.