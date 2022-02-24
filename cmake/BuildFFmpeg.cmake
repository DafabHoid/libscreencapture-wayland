################################################################################
# Copyright © 2022 by DafabHoid <github@dafaboid.de>
#
# SPDX-License-Identifier: GPL-3.0-or-later
################################################################################

# ffmpeg minimal build with static libraries
include(ExternalProject)
set(FFMPEG_OPTIMIZATION_FLAGS --disable-debug)
if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(FFMPEG_OPTIMIZATION_FLAGS --enable-debug=2 --disable-optimizations)
endif()
find_program(MAKE REQUIRED NAMES gmake nmake make)
ExternalProject_Add(ffmpeg
        PREFIX libav
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/ffmpeg
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --enable-gpl --enable-version3 --disable-programs --disable-doc
        --disable-swresample --disable-swscale --disable-postproc --disable-avdevice --disable-everything
        --enable-encoder=h264_vaapi
        --enable-muxer=rtsp --enable-muxer=mpegts
        --enable-filter=scale_vaapi --enable-filter=hwupload --enable-filter=hwmap
        --enable-protocol=file --enable-protocol=rtp
        --enable-libdrm --disable-xlib --disable-vdpau --prefix=<INSTALL_DIR>
        ${FFMPEG_OPTIMIZATION_FLAGS}
        BUILD_COMMAND     ${MAKE} -C <SOURCE_DIR>
        BUILD_ALWAYS      0
        INSTALL_COMMAND   ${MAKE} -C <SOURCE_DIR> install
        LOG_CONFIGURE 1 LOG_INSTALL 1
        )
ExternalProject_Get_Property(ffmpeg INSTALL_DIR)

# create cmake targets for every library
# pkg-config can't be used help here, because the .pc files are only generated after the install step of ffmpeg, but we need them now during configuration
# → specify library dependencies manually here
add_library(libav::util STATIC IMPORTED)
set_target_properties(libav::util PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libavutil.a)
target_include_directories(libav::util INTERFACE ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include/libavutil) # Trick for CMake to stop complaining about non-existent ${INSTALL_DIR}/include directory
target_link_libraries(libav::util INTERFACE Threads::Threads va-drm va drm m dl)

add_library(libav::codec STATIC IMPORTED)
set_target_properties(libav::codec PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libavcodec.a)
target_include_directories(libav::codec INTERFACE ${INSTALL_DIR}/include)
target_link_libraries(libav::codec INTERFACE libav::util Threads::Threads m va)

add_library(libav::format STATIC IMPORTED)
set_target_properties(libav::format PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libavformat.a)
target_include_directories(libav::format INTERFACE ${INSTALL_DIR}/include)
target_link_libraries(libav::format INTERFACE libav::codec libav::util m z)

add_library(libav::filter STATIC IMPORTED)
set_target_properties(libav::filter PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libavfilter.a)
target_include_directories(libav::filter INTERFACE ${INSTALL_DIR}/include)
target_link_libraries(libav::filter INTERFACE libav::util Threads::Threads m va)
