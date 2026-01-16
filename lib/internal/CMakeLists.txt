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
            stduuid
            spdlog::spdlog
            fmt::fmt
            picojson::picojson
    )

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

# Exported because mxl-common depends on this INTERFACE
# target. INTERFACE targets install no files.
install(TARGETS mxl-internal-headers
        EXPORT ${PROJECT_NAME}-targets
        COMPONENT ${PROJECT_NAME}-dev
    )

add_subdirectory(tests)
