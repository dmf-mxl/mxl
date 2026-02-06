# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

find_path(picojson_INCLUDE_DIR NAMES picojson/picojson.h DOC "The picojson include directory")
mark_as_advanced(picojson_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(picojson
    FOUND_VAR picojson_FOUND
    REQUIRED_VARS picojson_INCLUDE_DIR
)

if(picojson_FOUND)
    write_file("${CMAKE_CURRENT_BINARY_DIR}/picojson_wrapper/picojson/wrapper.h"
        "\
#pragma once
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored \"-Wmaybe-uninitialized\"
#include <picojson/picojson.h>
#pragma GCC diagnostic pop")

    set(picojson_INCLUDE_DIRS ${picojson_INCLUDE_DIR})
    if(NOT TARGET picojson::picojson)
        add_library(picojson::picojson INTERFACE IMPORTED)
        set_target_properties(picojson::picojson PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${picojson_INCLUDE_DIRS}"
        )
        set_target_properties(picojson::picojson PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/picojson_wrapper"
        )
    endif()
endif()
