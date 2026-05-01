# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

# Interface library for all targets that need to consume
# internal symbols from the mxl-internal/*.h header files
add_library(mxl-internal-headers INTERFACE)
target_compile_features(mxl-internal-headers
        INTERFACE
            cxx_std_20
    )
target_include_directories(mxl-internal-headers
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
target_link_libraries(mxl-internal-headers
        INTERFACE
            mxl-headers
    )

# These dependencies are only needed while building mxl itself. They are
# not part of the installed link interface for either shared or static builds.
target_link_libraries(mxl-internal-headers
        INTERFACE
            $<BUILD_INTERFACE:stduuid>
            $<BUILD_INTERFACE:picojson::picojson>
    )
# Shared builds link these privately into the library, so the installed
# package should not expose them. Static builds must keep them in the
# exported interface so consumers know what to link.
if(BUILD_SHARED_LIBS)
    target_link_libraries(mxl-internal-headers
            INTERFACE
                $<BUILD_INTERFACE:spdlog::spdlog>
                $<BUILD_INTERFACE:fmt::fmt>
        )
else()
    target_link_libraries(mxl-internal-headers
            INTERFACE
                spdlog::spdlog
                fmt::fmt
        )
endif()

# Internal implementation objects. These are consumed by libmxl and by internal
# tests, but are not installed or exported as a user-visible library.
add_library(mxl-internal-objects OBJECT)
target_compile_features(mxl-internal-objects
        PUBLIC
            cxx_std_20
    )
set_target_properties(mxl-internal-objects
        PROPERTIES 
            POSITION_INDEPENDENT_CODE ${MXL_POSITION_INDEPENDENT_CODE}
            VISIBILITY_INLINES_HIDDEN ON
            C_VISIBILITY_PRESET       hidden
            CXX_VISIBILITY_PRESET     hidden
            C_EXTENSIONS              OFF
            CXX_EXTENSIONS            OFF
    )
target_sources(mxl-internal-objects
        PRIVATE
            src/DomainWatcher.cpp
            src/FlowData.cpp
            src/FlowInfo.cpp
            src/FlowIoFactory.cpp
            src/FlowManager.cpp
            src/FlowOptionsParser.cpp
            src/FlowParser.cpp
            src/FlowReader.cpp
            src/FlowSynchronizationGroup.cpp
            src/FlowWriter.cpp
            src/Instance.cpp
            src/Logging.cpp
            src/MediaUtils.cpp
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

target_include_directories(mxl-internal-objects
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
target_link_libraries(mxl-internal-objects
        PRIVATE
            mxl-internal-headers
    )

# Enable LTO/IPO on target if enabled and supported
mxl_enable_target_ipo(mxl-internal-objects)

# Static installs need this exported interface target because mxl's imported
# link interface refers to private dependencies through it.
if (NOT BUILD_SHARED_LIBS)
    install(TARGETS mxl-internal-headers
            EXPORT ${PROJECT_NAME}-targets
            COMPONENT ${PROJECT_NAME}-dev
        )
endif()

if (BUILD_TESTS)
    add_subdirectory(tests)
endif()
