################################################################################
# Copyright © 2022-2023 by DafabHoid <github@dafaboid.de>
#
# SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
cmake_minimum_required(VERSION 3.18)
project(screencapture)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS False)

option(ENABLE_PORTAL_MODULE "Enable the xdg-desktop-portal module" ON)
option(ENABLE_PIPEWIRE_MODULE "Enable the PipeWire module" ON)
option(ENABLE_FFMPEG_MODULE "Enable the FFmpeg encoder module" ON)
option(ENABLE_GSTREAMER_MODULE "Enable the GStreamer encoder module" OFF)
option(BUILD_SHARED_LIBS "Build a shared library instead of a static one" OFF)
option(BUILD_FFMPEG "Build own ffmpeg libraries instead of using system-provided ones" OFF)
# use static C++ library by default to reduce possible ABI breakages at runtime
option(STATIC_LIBSTDC++ "Link with static C++ standard library" ON)

# set symbol visibility to hidden by default (prevents accidental linking against internal symbols)
if (NOT DEFINED CMAKE_CXX_VISIBILITY_PRESET AND
        NOT DEFINED CMAKE_VISIBILITY_INLINES_HIDDEN)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
endif ()


######################################
# Module definitions
######################################

add_library(screencapture-wayland-common INTERFACE
            common.hpp
            include/c_common.h
            include/module-portal.h
            include/module-pipewire.h)
target_include_directories(screencapture-wayland-common INTERFACE include/)

add_library(screencapture-wayland)
# remove unused code and enable RelRO
target_link_options(screencapture-wayland PRIVATE -Wl,-z,now -Wl,-gc-sections)
target_link_libraries(screencapture-wayland PUBLIC screencapture-wayland-common)

if (${BUILD_SHARED_LIBS})
    set(MODULE_SCOPE PRIVATE)
else ()
    set(MODULE_SCOPE PUBLIC)
endif ()

if(ENABLE_PORTAL_MODULE)
    add_subdirectory(PortalModule)
    target_link_libraries(screencapture-wayland ${MODULE_SCOPE} screencapture-module-portal)
endif()


if (ENABLE_PIPEWIRE_MODULE)
    add_subdirectory(PipeWireModule)
    target_link_libraries(screencapture-wayland ${MODULE_SCOPE} screencapture-module-pipewire)
endif()


if (ENABLE_FFMPEG_MODULE)
    add_subdirectory(FFMPEGModule)
    target_link_libraries(screencapture-wayland ${MODULE_SCOPE} screencapture-module-ffmpeg)
endif()

if (ENABLE_GSTREAMER_MODULE)
    add_subdirectory(GStreamerModule)
    target_link_libraries(screencapture-wayland ${MODULE_SCOPE} screencapture-module-gst)
endif()


######################################
# main binary
######################################

add_executable(screencapture main.cpp)
target_link_libraries(screencapture PRIVATE screencapture-wayland screencapture-module-pipewire)

# enable Full RELRO
target_link_options(screencapture PRIVATE -Wl,-z,now -Wl,-gc-sections -Wl,-as-needed)
if(${STATIC_LIBSTDC++})
    target_link_options(screencapture PRIVATE -static-libstdc++)
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    # clang needlessly vectorizes the string replace code, even though the string is quite small
    # disable vectorize for this file to reduce code size
    set_source_files_properties(xdg-desktop-portal.cpp PROPERTIES COMPILE_OPTIONS "-fno-vectorize")
endif()