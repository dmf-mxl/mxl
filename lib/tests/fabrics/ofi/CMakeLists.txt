# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
#
# SPDX-License-Identifier: Apache-2.0

if (NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 REQUIRED)
endif()

add_executable(mxl-fabrics-ofi-tests)

target_compile_features(mxl-fabrics-ofi-tests
        PRIVATE
            cxx_std_20
    )

set_target_properties(mxl-fabrics-ofi-tests
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )

target_sources(mxl-fabrics-ofi-tests
        PRIVATE
            test_Address.cpp
            test_Domain.cpp
            test_Provider.cpp
            test_Region.cpp
    )

target_link_libraries(mxl-fabrics-ofi-tests
        PRIVATE
            mxl
            mxl-internal-headers
            mxl-fabrics
            mxl-fabrics-objects
            Catch2::Catch2WithMain
            stduuid
    )

target_include_directories(mxl-fabrics-ofi-tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../fabrics/ofi/src/internal"
    )

include(CTest)
include(Catch)
catch_discover_tests(mxl-fabrics-ofi-tests)


