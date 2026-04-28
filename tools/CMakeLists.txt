# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(FetchContent)

FetchContent_Declare(
    simple_uri_parser
    GIT_REPOSITORY https://github.com/jholloc/simple-uri-parser.git
    GIT_TAG        v1.0.0
)

FetchContent_MakeAvailable(simple_uri_parser)
add_library(simple_uri_parser INTERFACE)

target_include_directories(simple_uri_parser
    INTERFACE
        ${simple_uri_parser_SOURCE_DIR}
)

add_subdirectory(mxl-info)
add_subdirectory(mxl-data-probe)
add_subdirectory(mxl-gst)

if (MXL_ENABLE_FABRICS_OFI)
    add_subdirectory(mxl-fabrics-demo)
endif ()
