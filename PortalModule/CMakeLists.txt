################################################################################
# Copyright © 2023 by DafabHoid <github@dafaboid.de>
#
# SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
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

add_library(screencapture-module-portal OBJECT
            xdg-desktop-portal.cpp
            xdg-desktop-portal.hpp)
target_link_libraries(screencapture-module-portal PRIVATE screencapture-wayland-common)
target_link_libraries(screencapture-module-portal ${MODULE_SCOPE} sdbus-c++)
set_property(TARGET screencapture-module-portal PROPERTY POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})