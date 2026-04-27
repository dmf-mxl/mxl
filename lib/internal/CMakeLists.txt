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

# Common code, used by both the libmxl and libmxl-fabrics library
# linking this does not give access to the internal headers.
add_library(mxl-common)
target_compile_features(mxl-common
        PUBLIC
            cxx_std_20
    )
set_target_properties(mxl-common
        PROPERTIES 
            POSITION_INDEPENDENT_CODE ${MXL_POSITION_INDEPENDENT_CODE}
            VISIBILITY_INLINES_HIDDEN ON
            C_VISIBILITY_PRESET       hidden
            CXX_VISIBILITY_PRESET     hidden
            C_EXTENSIONS              OFF
            CXX_EXTENSIONS            OFF
            VERSION                   "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            SOVERSION                 "${PROJECT_VERSION_MAJOR}"
    )
target_sources(mxl-common
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

target_include_directories(mxl-common
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
target_link_libraries(mxl-common
        PRIVATE
            mxl-internal-headers
    )

# Add common linker options
#mxl_add_common_target_link_options(mxl-common PRIVATE)
# Enable LTO/IPO on target if enabled and supported
mxl_enable_target_ipo(mxl-common)

install(TARGETS mxl-common EXPORT ${PROJECT_NAME}-targets
        COMPONENT ${PROJECT_NAME}-lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

# Static installs need this exported interface target because mxl-common's
# imported link interface refers to it. Shared installs do not export it.
if (NOT BUILD_SHARED_LIBS)
    install(TARGETS mxl-internal-headers
            EXPORT ${PROJECT_NAME}-targets
            COMPONENT ${PROJECT_NAME}-dev
        )
endif()

if (BUILD_TESTS)
    add_subdirectory(tests)
endif()
