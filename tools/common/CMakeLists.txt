# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

add_library(mxl-tools-common INTERFACE)
target_include_directories(mxl-tools-common
        INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
target_link_libraries(mxl-tools-common INTERFACE mxl-headers)
target_compile_features(mxl-tools-common INTERFACE cxx_std_20)
