# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

add_library(mxl-fabrics-headers INTERFACE)

target_include_directories(mxl-fabrics-headers
        INTERFACE 
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

install(TARGETS mxl-fabrics-headers EXPORT mxl-fabrics-targets
        COMPONENT ${PROJECT_NAME}-dev
    )

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT ${PROJECT_NAME}-dev
        FILES_MATCHING
            PATTERN "*.h"
    )

if (MXL_ENABLE_FABRICS_OFI)
    add_subdirectory(ofi)
endif ()

