# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

# Configure the version.h header file base on the current project version
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/mxl/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/mxl/version.h
        @ONLY
    )

add_library(mxl-headers INTERFACE)
target_compile_features(mxl-headers
        INTERFACE
            cxx_std_20
    )
target_include_directories(mxl-headers
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

# Install targets
install(TARGETS mxl-headers EXPORT ${PROJECT_NAME}-targets
        COMPONENT ${PROJECT_NAME}-dev
    )
install(DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT ${PROJECT_NAME}-dev
        FILES_MATCHING
            PATTERN "*.h"
    )
install(DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT ${PROJECT_NAME}-dev
        FILES_MATCHING
            PATTERN "*.h"
    )



add_library(mxl SHARED)
target_compile_features(mxl
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON      # Needed to bulid shared library later
            VISIBILITY_INLINES_HIDDEN ON
            C_VISIBILITY_PRESET       hidden
            CXX_VISIBILITY_PRESET     hidden
            C_EXTENSIONS              OFF
            CXX_EXTENSIONS            OFF
            VERSION                   "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            SOVERSION                 "${PROJECT_VERSION_MAJOR}"
    )
target_link_options(mxl
        PRIVATE
            "-Wl,--exclude-libs,ALL"
    )

target_sources(mxl
        PRIVATE
            src/flow.cpp
            src/mxl.cpp
            src/time.cpp
    )

target_link_libraries(mxl
        PUBLIC
            mxl-headers
        PRIVATE
            mxl-internal
    )

# Alias mxl to mxl::mxl so that this library can be used
# in lieu of a module from the local source tree
add_library(${PROJECT_NAME}::mxl ALIAS mxl)

if (MXL_FABRICS_OFI)
    add_subdirectory(fabrics)
endif()

add_subdirectory(tests)

# Install targets
install(TARGETS mxl EXPORT ${PROJECT_NAME}-targets
        COMPONENT ${PROJECT_NAME}-lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

include(CMakePackageConfigHelpers)

set(MXL_CMAKE_CONFIG_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

# Generate the config file and put it into the build directory.
configure_package_config_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/${PROJECT_NAME}-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
        INSTALL_DESTINATION ${MXL_CMAKE_CONFIG_DESTINATION}
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

# Generate the version file and put it into the build directory.
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )

# Install the generated config and version files.
install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        DESTINATION ${MXL_CMAKE_CONFIG_DESTINATION}
        COMPONENT ${PROJECT_NAME}-dev
    )

# Generate a pkg-config file
configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/libmxl.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/libmxl.pc
        @ONLY
    )

# Install the generated pkg-config file
install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/libmxl.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        COMPONENT ${PROJECT_NAME}-dev
    )

# Make the project importable from the install directory
install(EXPORT ${PROJECT_NAME}-targets
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${MXL_CMAKE_CONFIG_DESTINATION}
        COMPONENT ${PROJECT_NAME}-dev
    )

add_subdirectory(internal)
