# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

cmake_path(RELATIVE_PATH CMAKE_INSTALL_FULL_LIBDIR
           BASE_DIRECTORY "${CMAKE_INSTALL_FULL_BINDIR}"
           OUTPUT_VARIABLE MXL_TOOLS_INSTALL_RPATH
    )

if(APPLE)
    set(MXL_TOOLS_INSTALL_RPATH "@loader_path/${MXL_TOOLS_INSTALL_RPATH}")
else()
    set(MXL_TOOLS_INSTALL_RPATH "$ORIGIN/${MXL_TOOLS_INSTALL_RPATH}")
endif()

add_subdirectory(mxl-info)
add_subdirectory(mxl-data-probe)
add_subdirectory(mxl-gst)

if (MXL_ENABLE_FABRICS_OFI)
    add_subdirectory(mxl-fabrics-demo)
endif ()
