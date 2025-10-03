# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
#
# SPDX-License-Identifier: Apache-2.0

add_library(mxl-internal STATIC)
target_compile_features(mxl-internal
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl-internal
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    OFF
            C_VISIBILITY_PRESET          default
            CXX_VISIBILITY_PRESET        default
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
            VERSION                      "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            SOVERSION                    "${PROJECT_VERSION_MAJOR}"
    )

target_sources(mxl-internal
        PRIVATE
            DomainWatcher.cpp
            Flow.cpp
            FlowData.cpp
            FlowIoFactory.cpp
            FlowManager.cpp
            FlowParser.cpp
            FlowReader.cpp
            FlowWriter.cpp
            Instance.cpp
            Logging.cpp
            PathUtils.cpp
            PosixContinuousFlowReader.cpp
            PosixContinuousFlowWriter.cpp
            PosixDiscreteFlowReader.cpp
            PosixDiscreteFlowWriter.cpp
            PosixFlowIoFactory.cpp
            SharedMemory.cpp
            Sync.cpp
            Thread.cpp
            Time.cpp
            Timing.cpp
        )

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

target_include_directories(mxl-internal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(mxl-internal
        PRIVATE
            stduuid
            spdlog::spdlog
            fmt::fmt
            picojson::picojson
    )

