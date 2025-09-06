# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
#
# SPDX-License-Identifier: Apache-2.0

if (NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 REQUIRED)
endif()


project(mxl-fabrics-ofi-tests)

add_executable(${PROJECT_NAME})
target_compile_features(${PROJECT_NAME}
        PRIVATE
            cxx_std_20
    )

set_target_properties(${PROJECT_NAME}
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )

target_sources(${PROJECT_NAME}
    PRIVATE
    test_Address.cpp
    test_Domain.cpp
    test_Provider.cpp
    test_Region.cpp
    test_TargetInfo.cpp
    test_Connection.cpp


)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        mxl
        mxl-fabrics-static
        Catch2::Catch2WithMain
        stduuid
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../fabrics/ofi/src/internal"
)

include(CTest)
include(Catch)
catch_discover_tests(${PROJECT_NAME})


