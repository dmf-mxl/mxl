# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

# Add the test executable
add_executable(mxl-internal-tests)
target_compile_features(mxl-internal-tests
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl-internal-tests
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )

target_sources(mxl-internal-tests
        PRIVATE
            test_domainwatcher.cpp
            test_flowmanager.cpp
            test_options.cpp
            test_sharedmem.cpp
    )

target_link_libraries(mxl-internal-tests
        PRIVATE
            mxl-common
            mxl-internal-headers
    )

if (NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 REQUIRED)
endif()

if (NOT TARGET stduuid)
    find_package(stduuid CONFIG REQUIRED)
endif ()

if (NOT TARGET spdlog::spdlog)
    find_package(spdlog CONFIG REQUIRED)
endif ()

if (NOT TARGET fmt::fmt)
    find_package(fmt CONFIG REQUIRED)
endif()

if (NOT TARGET picojson::picojson)
    find_package(picojson REQUIRED)
endif()

if (NOT TARGET PcapPlusPlus::Pcap++)
    find_package(PcapPlusPlus CONFIG)
endif()


target_link_libraries(mxl-internal-tests
        PRIVATE
            stduuid
            spdlog::spdlog
            fmt::fmt
            picojson::picojson
            Catch2::Catch2WithMain
    )

if (TARGET PcapPlusPlus::Pcap++)
    target_link_libraries(mxl-internal-tests
            PRIVATE
                PcapPlusPlus::Pcap++
        )
endif()

# Enable testing using CTest
include(CTest)
include(Catch)
catch_discover_tests(mxl-internal-tests)

# Copy test files to the build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
