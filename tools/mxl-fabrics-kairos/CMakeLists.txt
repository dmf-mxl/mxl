# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
#
# SPDX-License-Identifier: Apache-2.0

find_package(stduuid CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(mxl-kairos STATIC)
add_library(mxl::kairos ALIAS mxl-kairos)

target_compile_features(mxl-kairos
        PRIVATE
            cxx_std_20
    )
target_sources(mxl-kairos
        PRIVATE
            mxl_kairos.cpp
            mxl_kairos.hpp
    )
target_include_directories(mxl-kairos
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )
target_link_libraries(mxl-kairos
        PRIVATE
            mxl
            mxl-fabrics
            mxl-internal-headers
            stduuid
            spdlog::spdlog
    )
set_target_properties(mxl-kairos
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_EXTENSIONS OFF
    )
