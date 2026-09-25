# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

# Add the test executable
add_executable(mxl-tests)
target_compile_features(mxl-tests
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl-tests
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )

target_sources(mxl-tests
        PRIVATE
            test_flow_sync_groups.cpp
            test_flows.cpp
            test_events.cpp
            test_flows_timing.cpp
            test_instance.cpp
            test_time.cpp
            test_read_write_conflict.cpp
    )

target_link_libraries(mxl-tests
        PRIVATE
            mxl
            mxl-internal-headers
            mxl-internal-objects
    )

if (NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 REQUIRED)
endif()

if (NOT TARGET stduuid)
    find_package(stduuid CONFIG REQUIRED)
endif ()

if (NOT TARGET picojson::picojson)
    find_package(picojson REQUIRED)
endif()

if (NOT TARGET PcapPlusPlus::Pcap++)
    find_package(PcapPlusPlus CONFIG REQUIRED)
endif()


target_link_libraries(mxl-tests
        PRIVATE
            mxl
            stduuid
            picojson::picojson
            Catch2::Catch2WithMain
            PcapPlusPlus::Pcap++
    )

# Helper process for event IPC tests.
add_executable(mxl-event-ipc-worker)
target_sources(mxl-event-ipc-worker
        PRIVATE
            event_ipc_worker.cpp
    )
target_compile_features(mxl-event-ipc-worker
        PRIVATE
            cxx_std_20
    )
target_link_libraries(mxl-event-ipc-worker
        PRIVATE
            mxl
    )
add_dependencies(mxl-tests mxl-event-ipc-worker)
target_compile_definitions(mxl-tests
        PRIVATE
            MXL_EVENT_IPC_WORKER="$<TARGET_FILE:mxl-event-ipc-worker>"
    )

# Enable testing using CTest
include(CTest)
include(Catch)
catch_discover_tests(mxl-tests)

set(MXL_LINKER_FLAG "")
if(CMAKE_LINKER_TYPE STREQUAL "LLD")
  set(MXL_LINKER_FLAG "${CMAKE_C_USING_LINKER_LLD}")
endif()
add_test(
  NAME "pkg-config C consumer can link and run"
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_pkgconfig_c_link.sh
          ${CMAKE_BINARY_DIR}
          ${VCPKG_INSTALLED_DIR}
          ${VCPKG_TARGET_TRIPLET}
          ${BUILD_SHARED_LIBS}
	  ${MXL_ENABLE_FABRICS_OFI}
          MXL_LINKER_FLAG=${MXL_LINKER_FLAG}
          ${CMAKE_C_COMPILER}
)
set_tests_properties(
  "pkg-config C consumer can link and run"
  PROPERTIES
    ENVIRONMENT
    "PKG_CONFIG_PATH=$ENV{PKG_CONFIG_PATH}"
)

# Copy test files to the build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# ----------------------------------------------------------------------------
# Compile all public C headers (aggregated includes) with strict C17 warnings.
# ----------------------------------------------------------------------------

# OBJECT library to just compile (no link step)
add_library(mxl-all-headers OBJECT all_headers.c)

set_target_properties(mxl-all-headers PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_include_directories(mxl-all-headers PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

target_compile_options(mxl-all-headers PRIVATE
    -Wall -Wextra -Werror -pedantic
    -Wstrict-prototypes
)

if (MXL_ENABLE_FABRICS_OFI)
    add_subdirectory(fabrics)
endif ()
