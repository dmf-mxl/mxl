# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

# Tests of the cuda-linear payload backend. They only use the public API and link only libmxl, so that the loader in
# libmxl finds the backend next to it, as in an installed application. They are skipped when no CUDA device is
# visible.

if (NOT TARGET CUDA::cudart_static)
    find_package(CUDAToolkit REQUIRED)
endif()

add_executable(mxl-cuda-reader-helper cuda_reader_helper.cpp)
add_executable(mxl-cuda-writer-helper cuda_writer_helper.cpp)
add_executable(mxl-cuda-tests test_cuda_payload.cpp)

foreach(target mxl-cuda-reader-helper mxl-cuda-writer-helper mxl-cuda-tests)
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

# The default paths are the build tree. A packaged test image can point these at the installed locations.
set(MXL_CUDA_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data" CACHE PATH
        "Directory of the flow definitions used by the CUDA payload tests.")
set(MXL_CUDA_READER_HELPER_PATH "" CACHE FILEPATH
        "Path of mxl-cuda-reader-helper used by the CUDA payload tests. Empty means the built executable.")
set(MXL_CUDA_CROSS_CONTAINER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../../examples/cuda-cross-container-test.sh" CACHE FILEPATH
        "Script that runs the CUDA writer and reader in two containers.")
if (MXL_CUDA_READER_HELPER_PATH)
    set(mxl_cuda_reader_helper "${MXL_CUDA_READER_HELPER_PATH}")
else()
    set(mxl_cuda_reader_helper "$<TARGET_FILE:mxl-cuda-reader-helper>")
endif()
target_compile_definitions(mxl-cuda-tests
        PRIVATE
            MXL_CUDA_READER_HELPER="${mxl_cuda_reader_helper}"
            MXL_CUDA_CROSS_CONTAINER_SCRIPT="${MXL_CUDA_CROSS_CONTAINER_SCRIPT}"
            MXL_TEST_DATA_DIR="${MXL_CUDA_TEST_DATA_DIR}"
    )

include(Catch)
catch_discover_tests(mxl-cuda-tests)
