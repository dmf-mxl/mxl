# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

# Add the test executable
add_executable(mxl-internal-tests)
set_target_properties(mxl-internal-tests
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
            C_STANDARD                   ${MXL_C_STANDARD}
            CXX_STANDARD                 ${MXL_CXX_STANDARD}
    )

target_sources(mxl-internal-tests
        PRIVATE
            test_domainwatcher.cpp
            test_flowmanager.cpp
            test_instance.cpp
            test_options.cpp
            test_payload_backend.cpp
            test_sharedmem.cpp
    )

target_link_libraries(mxl-internal-tests
        PRIVATE
            mxl-internal-objects
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

# Payload backends used by test_payload_backend.cpp and by the flow tests in lib/tests. libmxl only loads backends
# from the directory of the binary that contains its code. The flow test executable links the internal objects
# directly, so that binary is the executable itself, and the backends are built next to it. The internal tests pass
# the same directory to their own PayloadBackendLoader. The file names follow the libmxl-payload-<name>.so convention
# that libmxl expects. These libraries are never installed.
function(mxl_add_test_payload_backend backendName sourceFile)
    set(target mxl-test-payload-${backendName})
    add_library(${target} MODULE ${sourceFile})
    set_target_properties(${target}
            PROPERTIES
                OUTPUT_NAME                 mxl-payload-${backendName}
                PREFIX                      lib
                SUFFIX                      .so
                LIBRARY_OUTPUT_DIRECTORY    $<TARGET_FILE_DIR:mxl-tests>
                VISIBILITY_INLINES_HIDDEN   ON
                CXX_VISIBILITY_PRESET       hidden
                CXX_EXTENSIONS              OFF
                CXX_STANDARD                ${MXL_CXX_STANDARD}
        )
    target_include_directories(${target}
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/../include
        )
    # picojson::picojson only carries the wrapper directory, whose header includes picojson.h from picojson's own
    # include directory.
    target_include_directories(${target}
            SYSTEM PRIVATE
                ${picojson_INCLUDE_DIR}
        )
    target_link_libraries(${target}
            PRIVATE
                mxl-headers
                picojson::picojson
        )
    add_dependencies(mxl-internal-tests ${target})
endfunction()

mxl_add_test_payload_backend(test-file payload_backends/TestFileBackend.cpp)
target_compile_definitions(mxl-test-payload-test-file PRIVATE MXL_TEST_BACKEND_NAME="test-file")

# Same backend in a library whose name does not match the name the backend reports.
mxl_add_test_payload_backend(test-bad-name payload_backends/TestFileBackend.cpp)
target_compile_definitions(mxl-test-payload-test-bad-name PRIVATE MXL_TEST_BACKEND_NAME="some-other-name")

# A library without the backend entry point.
mxl_add_test_payload_backend(test-no-entry payload_backends/NoEntryPointBackend.cpp)

target_compile_definitions(mxl-internal-tests
        PRIVATE
            MXL_TEST_PAYLOAD_BACKEND_DIR="$<TARGET_FILE_DIR:mxl-test-payload-test-file>"
    )

# Enable testing using CTest
include(CTest)
include(Catch)
catch_discover_tests(mxl-internal-tests)

# Copy test files to the build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
