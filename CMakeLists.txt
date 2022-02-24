################################################################################
# Copyright © 2022 by DafabHoid <github@dafaboid.de>
#
# SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
cmake_minimum_required(VERSION 3.19)
project(screencapture)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS False)

option(BUILD_FFMPEG "Build own ffmpeg libraries instead of using system-provided ones" OFF)
option(USE_LIBC++ "Use libc++ as C++ standard library instead of libstdc++ when compiling with clang" ON)
# use static C++ library by default to reduce possible ABI breakages at runtime
option(STATIC_LIBSTDC++ "Link with static C++ standard library" ON)

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    if (${USE_LIBC++})
        set(stdlib libc++)
        set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -stdlib=${stdlib})
    endif()
    find_program(lld NAMES lld)
    if(NOT lld AND ${USE_LIBC++} AND ${STATIC_LIBSTDC++})
        message(WARNING "LLVM linker lld not found. It is required to properly link the libc++ library statically")
    endif()
endif()
add_subdirectory(sdbus-cpp)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(pipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)

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
endif()

# main binary
add_executable(screencapture main.cpp xdg-desktop-portal.cpp common.hpp PipeWireStream.cpp libavoutput.cpp VAAPIEncoder.cpp VAAPIEncoder.hpp VAAPIScaler.cpp VAAPIScaler.hpp Muxer.cpp Muxer.hpp libavcommon.hpp BlockingRingbuffer.hpp PipeWireStream.hpp xdg-desktop-portal.h)
if(${BUILD_FFMPEG})
    add_dependencies(screencapture ffmpeg)
endif()
target_include_directories(screencapture PRIVATE ringbuffer/include/)
target_compile_options(screencapture PRIVATE -march=x86-64-v3 -mtune=native -save-temps=obj -masm=intel)
target_link_libraries(screencapture PRIVATE sdbus-c++ PkgConfig::pipewire ${FFMPEG_LIBS} Threads::Threads)

# enable Full RELRO
target_link_options(screencapture PRIVATE -Wl,-z,now)
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