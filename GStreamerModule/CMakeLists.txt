################################################################################
# Copyright © 2023 by DafabHoid <github@dafaboid.de>
#
# SPDX-License-Identifier: GPL-3.0-or-later
################################################################################

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(gstcore REQUIRED IMPORTED_TARGET gstreamer-base-1.0)
pkg_check_modules(gstvideo REQUIRED IMPORTED_TARGET gstreamer-video-1.0)
pkg_check_modules(gstapp REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
set(GSTREAMER_LIBS
        PkgConfig::gstcore
        PkgConfig::gstapp
        PkgConfig::gstvideo)

add_library(screencapture-module-gst OBJECT ../common.hpp
        GstOutput.cpp
        GstOutput.hpp)
target_link_libraries(screencapture-module-gst PUBLIC ${GSTREAMER_LIBS} Threads::Threads)
set_property(TARGET screencapture-module-gst PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})