# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

# Tests of the cuda-linear payload backend. They only use the public API and link only libmxl, so that the loader in
# libmxl finds the backend next to it, as in an installed application. They are skipped when no CUDA device is
# visible.

if (NOT TARGET CUDA::cudart_static)
    find_package(CUDAToolkit REQUIRED)
endif()

add_executable(mxl-cuda-reader-helper cuda_reader_helper.cpp)
add_executable(mxl-cuda-tests test_cuda_payload.cpp)

foreach(target mxl-cuda-reader-helper mxl-cuda-tests)
    set_target_properties(${target}
            PROPERTIES
                C_EXTENSIONS    OFF
                CXX_EXTENSIONS  OFF
                C_STANDARD      ${MXL_C_STANDARD}
                CXX_STANDARD    ${MXL_CXX_STANDARD}
        )
    target_link_libraries(${target}
            PRIVATE
                mxl
                CUDA::cudart_static
        )
    add_dependencies(${target} mxl-payload-cuda-linear)
endforeach()

target_link_libraries(mxl-cuda-tests
        PRIVATE
            Catch2::Catch2WithMain
    )
add_dependencies(mxl-cuda-tests mxl-cuda-reader-helper)
target_compile_definitions(mxl-cuda-tests
        PRIVATE
            MXL_CUDA_READER_HELPER="$<TARGET_FILE:mxl-cuda-reader-helper>"
            MXL_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data"
    )

include(Catch)
catch_discover_tests(mxl-cuda-tests)
