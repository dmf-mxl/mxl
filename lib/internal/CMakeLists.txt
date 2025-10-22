# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

add_library(mxl-internal SHARED)
target_compile_features(mxl-internal
        PUBLIC
            cxx_std_20
    )
set_target_properties(mxl-internal
        PROPERTIES 
            POSITION_INDEPENDENT_CODE ON      # Needed to bulid shared library later
            VISIBILITY_INLINES_HIDDEN ON
            C_EXTENSIONS              OFF
            CXX_EXTENSIONS            OFF
            VERSION                   "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            SOVERSION                 "${PROJECT_VERSION_MAJOR}"
    )
target_sources(mxl-internal
        PRIVATE
            src/DomainWatcher.cpp
            src/Flow.cpp
            src/FlowData.cpp
            src/FlowIoFactory.cpp
            src/FlowManager.cpp
            src/FlowParser.cpp
            src/FlowReader.cpp
            src/FlowWriter.cpp
            src/Instance.cpp
            src/Logging.cpp
            src/PathUtils.cpp
            src/PosixContinuousFlowReader.cpp
            src/PosixContinuousFlowWriter.cpp
            src/PosixDiscreteFlowReader.cpp
            src/PosixDiscreteFlowWriter.cpp
            src/PosixFlowIoFactory.cpp
            src/SharedMemory.cpp
            src/Sync.cpp
            src/Thread.cpp
            src/Time.cpp
            src/Timing.cpp
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

target_include_directories(mxl-internal
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
target_link_libraries(mxl-internal
        PUBLIC
            mxl-headers
            stduuid
            spdlog::spdlog
            fmt::fmt
            picojson::picojson
    )

install(TARGETS mxl-internal EXPORT ${PROJECT_NAME}-targets
        COMPONENT ${PROJECT_NAME}-lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

add_subdirectory(tests)
