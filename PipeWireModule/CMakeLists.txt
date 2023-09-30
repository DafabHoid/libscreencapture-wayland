################################################################################
# Copyright © 2023 by DafabHoid <github@dafaboid.de>
#
# SPDX-License-Identifier: GPL-3.0-or-later
################################################################################

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(pipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)

add_library(screencapture-module-pipewire OBJECT ../common.hpp ../include/c_common.h
            PipeWireStream.cpp
            PipeWireStream.hpp
            ../include/module-pipewire.h)
target_include_directories(screencapture-module-pipewire PUBLIC ../include/)
target_link_libraries(screencapture-module-pipewire PUBLIC PkgConfig::pipewire Threads::Threads)
set_property(TARGET screencapture-module-pipewire PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
