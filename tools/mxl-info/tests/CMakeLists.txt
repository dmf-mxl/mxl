# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

add_executable(mxl-info-tests)
set_target_properties(mxl-info-tests
        PROPERTIES
            POSITION_INDEPENDENT_CODE   ON
            VISIBILITY_INLINES_HIDDEN   ON
            C_VISIBILITY_PRESET         hidden
            CXX_VISIBILITY_PRESET       hidden
            C_EXTENSIONS                OFF
            CXX_EXTENSIONS              OFF
            C_STANDARD                  ${MXL_C_STANDARD}
            CXX_STANDARD                ${MXL_CXX_STANDARD}
    )

target_sources(mxl-info-tests
        PRIVATE
            test_latency.cpp
            test_wait_for_next_commit.cpp
    )

# The flow definitions from the library tests.
target_compile_definitions(mxl-info-tests
        PRIVATE
            MXL_INFO_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/lib/tests/data"
    )

# Latency.hpp is in the parent directory.
target_include_directories(mxl-info-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

if (NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 REQUIRED)
endif ()

target_link_libraries(mxl-info-tests
        PRIVATE
            mxl
            Catch2::Catch2WithMain
    )

include(CTest)
include(Catch)
catch_discover_tests(mxl-info-tests TEST_PREFIX "mxl-info: ")
