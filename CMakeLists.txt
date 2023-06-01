################################################################################
# Copyright © 2022 by DafabHoid <github@dafaboid.de>
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
option(ENABLE_GSTREAMER_MODULE "Enable the GStreamer encoder module" ON)
option(BUILD_SHARED_LIBS "Build a shared library instead of a static one" OFF)
option(BUILD_FFMPEG "Build own ffmpeg libraries instead of using system-provided ones" OFF)
option(USE_LIBC++ "Use libc++ as C++ standard library instead of libstdc++ when compiling with clang" OFF)
# use static C++ library by default to reduce possible ABI breakages at runtime
option(STATIC_LIBSTDC++ "Link with static C++ standard library" ON)

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" AND ${USE_LIBC++})
    # use libc++ as stdlib for all source files
    set(stdlib libc++)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=${stdlib}")

    find_program(lld NAMES lld)
    if(NOT lld AND ${STATIC_LIBSTDC++})
        message(WARNING "LLVM linker lld not found. It is required to properly link the libc++ library statically")
    endif()
endif()

# set symbol visibility to hidden by default (prevents accidental linking against internal symbols)
if (NOT DEFINED CMAKE_CXX_VISIBILITY_PRESET AND
        NOT DEFINED CMAKE_VISIBILITY_INLINES_HIDDEN)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
endif ()

######################################
# Dependencies
######################################

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

if(ENABLE_PORTAL_MODULE)
    function(ImportSDBusCpp)
        # always build this statically, even when otherwise building shared libraries
        # this is needed because sdbus-c++ does not work well with VISIBILITY_PRESET "hidden"
        set(BUILD_SHARED_LIBS OFF)
        add_subdirectory(sdbus-cpp)
    endfunction()
    # use function to create a new scope for variables
    ImportSDBusCpp()
    # set the PIC flag correctly (is always off otherwise, because it is a static library)
    # but we include it in our library, so it must match in position independence
    set_target_properties(sdbus-c++-objlib PROPERTIES POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
endif()

if(ENABLE_PIPEWIRE_MODULE)
    pkg_check_modules(pipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)
endif()

if (ENABLE_FFMPEG_MODULE)
if (${BUILD_FFMPEG})
    include(cmake/BuildFFmpeg.cmake)
    set(FFMPEG_LIBS
            libav::codec
            libav::format
            libav::filter
            libav::util)
else()
    pkg_check_modules(libavutil REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(libavcodec REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(libavformat REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(libavfilter REQUIRED IMPORTED_TARGET libavfilter)
    set(FFMPEG_LIBS
            PkgConfig::libavcodec
            PkgConfig::libavformat
            PkgConfig::libavfilter
            PkgConfig::libavutil)
endif() # BUILD_FFMPEG
endif() # ENABLE_FFMPEG_MODULE

if (ENABLE_GSTREAMER_MODULE)
    pkg_check_modules(gstcore REQUIRED IMPORTED_TARGET gstreamer-base-1.0)
    pkg_check_modules(gstvideo REQUIRED IMPORTED_TARGET gstreamer-video-1.0)
    pkg_check_modules(gstapp REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
    set(GSTREAMER_LIBS
            PkgConfig::gstcore
            PkgConfig::gstapp
            PkgConfig::gstvideo)
endif()



######################################
# Module definitions
######################################

add_library(screencapture-wayland common.hpp include/c_common.h)
# remove unused code and enable RelRO
target_link_options(screencapture-wayland PRIVATE -Wl,-z,now -Wl,-gc-sections)


if(ENABLE_PORTAL_MODULE)
    add_library(screencapture-module-portal OBJECT common.hpp include/c_common.h
            xdg-desktop-portal.cpp xdg-desktop-portal.hpp
            include/module-portal.h)
    target_include_directories(screencapture-module-portal PUBLIC include/)
    if(BUILD_SHARED_LIBS)
        target_link_libraries(screencapture-module-portal PRIVATE sdbus-c++)
    else()
        # for a static library, propagate this dependency to anyone linking against us, because
        # linking a static library (this) against another one (sdbus-c++) does not do anything
        target_link_libraries(screencapture-module-portal PUBLIC sdbus-c++)
    endif()
    set_property(TARGET screencapture-module-portal PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})

    target_link_libraries(screencapture-wayland PUBLIC screencapture-module-portal)
endif()


if (ENABLE_PIPEWIRE_MODULE)
    add_library(screencapture-module-pipewire OBJECT common.hpp include/c_common.h
            PipeWireStream.cpp PipeWireStream.hpp
            include/module-pipewire.h)
    target_include_directories(screencapture-module-pipewire PUBLIC include/)
    target_link_libraries(screencapture-module-pipewire PUBLIC PkgConfig::pipewire)
    set_property(TARGET screencapture-module-pipewire PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})

    target_link_libraries(screencapture-wayland PUBLIC screencapture-module-pipewire)
endif()


if (ENABLE_FFMPEG_MODULE)
    add_library(screencapture-module-ffmpeg OBJECT libavcommon.hpp
            BlockingRingbuffer.hpp
            FFmpegOutput.cpp FFmpegOutput.hpp
            VAAPIEncoder.cpp VAAPIEncoder.hpp
            VAAPIScaler.cpp VAAPIScaler.hpp
            Muxer.cpp Muxer.hpp
            ThreadedWrapper.inc ThreadedWrapper.hpp)
    target_link_libraries(screencapture-module-ffmpeg PUBLIC ${FFMPEG_LIBS} Threads::Threads)
    set_property(TARGET screencapture-module-ffmpeg PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
    if(${BUILD_FFMPEG})
        add_dependencies(screencapture-module-ffmpeg ffmpeg)
    endif()

    target_link_libraries(screencapture-wayland PUBLIC screencapture-module-ffmpeg)
endif()

if (ENABLE_GSTREAMER_MODULE)
    add_library(screencapture-module-gst OBJECT common.hpp BlockingRingbuffer.hpp
            ThreadedWrapper.inc ThreadedWrapper.hpp
            GstOutput.cpp GstOutput.hpp)
    target_link_libraries(screencapture-module-gst PUBLIC ${GSTREAMER_LIBS} Threads::Threads)
    set_property(TARGET screencapture-module-gst PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
    target_link_libraries(screencapture-wayland PUBLIC screencapture-module-gst)
endif()


######################################
# main binary
######################################

add_executable(screencapture main.cpp)
target_link_libraries(screencapture PRIVATE screencapture-wayland)

# enable Full RELRO
target_link_options(screencapture PRIVATE -Wl,-z,now -Wl,-gc-sections -Wl,-as-needed)
if(${STATIC_LIBSTDC++})
    target_link_options(screencapture PRIVATE -static-libstdc++)
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    # clang needlessly vectorizes the string replace code, even though the string is quite small
    # disable vectorize for this file to reduce code size
    set_source_files_properties(xdg-desktop-portal.cpp PROPERTIES COMPILE_OPTIONS "-fno-vectorize")
    # use LLVM linker (faster than gnu ld)
    if(${USE_LIBC++} AND ${STATIC_LIBSTDC++})
        # link c++abi statically too
        target_link_options(screencapture PRIVATE -Wl,--push-state,-Bstatic -lc++abi -Wl,--pop-state)
    endif()
    if(lld)
        target_link_options(screencapture PRIVATE -fuse-ld=lld)
    endif()
endif()
