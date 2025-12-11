# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
 
if (NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 REQUIRED)
endif()

add_executable(mxl-fabrics-tests)

target_compile_features(mxl-fabrics-tests
        PRIVATE
            cxx_std_20
    )

set_target_properties(mxl-fabrics-tests
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )

target_sources(mxl-fabrics-tests
        PRIVATE
            test_basics.cpp
    )

target_link_libraries(mxl-fabrics-tests
        PRIVATE
            mxl
            mxl-common
            mxl-internal-headers
            mxl-fabrics
            mxl-fabrics-objects
            Catch2::Catch2WithMain
            stduuid
    )


if (MXL_ENABLE_FABRICS_OFI)
    target_include_directories(mxl-fabrics-tests
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../../fabrics/ofi/src/internal"
        )
    target_compile_definitions(mxl-fabrics-tests
            PRIVATE
            -DMXL_FABRICS_OFI
        )

    add_subdirectory(ofi)
endif()


include(CTest)
include(Catch)
catch_discover_tests(mxl-fabrics-tests)
