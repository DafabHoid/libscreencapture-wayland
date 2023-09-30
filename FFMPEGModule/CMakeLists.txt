################################################################################
# Copyright © 2022-2023 by DafabHoid <github@dafaboid.de>
#
# SPDX-License-Identifier: GPL-3.0-or-later
################################################################################

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

if (${BUILD_FFMPEG})
    include(../cmake/BuildFFmpeg.cmake)
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

add_library(screencapture-module-ffmpeg OBJECT
        libavcommon.hpp
        BlockingRingbuffer.hpp
        FFmpegOutput.cpp
        FFmpegOutput.hpp
        VAAPIEncoder.cpp
        VAAPIEncoder.hpp
        VAAPIScaler.cpp
        VAAPIScaler.hpp
        Muxer.cpp
        Muxer.hpp
        ThreadedWrapper.inc
        ThreadedWrapper.hpp)
target_link_libraries(screencapture-module-ffmpeg PUBLIC ${FFMPEG_LIBS} Threads::Threads)
set_property(TARGET screencapture-module-ffmpeg PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
if(${BUILD_FFMPEG})
    add_dependencies(screencapture-module-ffmpeg ffmpeg)
endif()
