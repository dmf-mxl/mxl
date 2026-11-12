# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

# The "cuda-linear" payload backend. libmxl loads it by name from its own directory when a flow asks for it, so it is
# built next to libmxl and installed with it. libmxl itself does not link against CUDA.

if (NOT TARGET CUDA::cudart_static)
    find_package(CUDAToolkit REQUIRED)
endif()

if (NOT TARGET picojson::picojson)
    find_package(picojson REQUIRED)
endif()

# The header of the storage type the backend reports, cuda-device-pointer, which applications include to read the
# device and the slots' device pointers from MXL's reserved bytes. It needs MXL's headers, not CUDA's, and is installed
# with the backend, not with libmxl, whose headers know nothing of CUDA.
add_library(mxl-cuda-device-pointer-headers INTERFACE)
target_include_directories(mxl-cuda-device-pointer-headers
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
target_link_libraries(mxl-cuda-device-pointer-headers
        INTERFACE
            mxl-headers
    )
install(DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT ${PROJECT_NAME}-dev
        FILES_MATCHING
            PATTERN "*.h"
    )

add_library(mxl-payload-cuda-linear MODULE)
set_target_properties(mxl-payload-cuda-linear
        PROPERTIES
            OUTPUT_NAME                 mxl-payload-cuda-linear
            PREFIX                      lib
            SUFFIX                      .so
            LIBRARY_OUTPUT_DIRECTORY    $<TARGET_FILE_DIR:mxl>
            POSITION_INDEPENDENT_CODE   ON
            VISIBILITY_INLINES_HIDDEN   ON
            C_VISIBILITY_PRESET         hidden
            CXX_VISIBILITY_PRESET       hidden
            C_EXTENSIONS                OFF
            CXX_EXTENSIONS              OFF
            C_STANDARD                  ${MXL_C_STANDARD}
            CXX_STANDARD                ${MXL_CXX_STANDARD}
    )

target_sources(mxl-payload-cuda-linear
        PRIVATE
            CudaLinearBackend.cpp
    )

target_include_directories(mxl-payload-cuda-linear
        PRIVATE
            ${PROJECT_SOURCE_DIR}/lib/internal/include
    )
# picojson::picojson only carries the wrapper directory, whose header includes picojson.h from picojson's own
# include directory.
target_include_directories(mxl-payload-cuda-linear
        SYSTEM PRIVATE
            ${picojson_INCLUDE_DIR}
    )

# The static CUDA runtime keeps the backend's only runtime dependency the NVIDIA driver (libcuda.so).
target_link_libraries(mxl-payload-cuda-linear
        PRIVATE
            mxl-cuda-device-pointer-headers
            picojson::picojson
            CUDA::cudart_static
    )

mxl_add_common_target_link_options(mxl-payload-cuda-linear PRIVATE)

install(TARGETS mxl-payload-cuda-linear
        COMPONENT ${PROJECT_NAME}-lib
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

# The backend's tests, which use only the public API and the header of its storage type.
if (BUILD_TESTS)
    add_subdirectory(tests)
endif()
