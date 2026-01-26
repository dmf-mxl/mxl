# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

message ("-- Building fabrics library with libfabric/ofi")

find_package(PkgConfig REQUIRED)

pkg_check_modules(libfabric REQUIRED IMPORTED_TARGET libfabric)

if (NOT TARGET spdlog::spdlog)
    find_package(spdlog CONFIG REQUIRED)
endif()

if (NOT TARGET fmt::fmt)
    find_package(fmt CONFIG REQUIRED)
endif()

add_library(mxl-fabrics-objects OBJECT)
target_compile_features(mxl-fabrics-objects
        PRIVATE
            cxx_std_20
    )
target_sources(mxl-fabrics-objects
        PRIVATE
            src/fabrics.cpp
            src/internal/Exception.cpp
            src/internal/Provider.cpp
            src/internal/FabricVersion.cpp
            src/internal/FabricInfo.cpp
            src/internal/Fabric.cpp
            src/internal/Domain.cpp
            src/internal/Region.cpp
            src/internal/MemoryRegion.cpp
            src/internal/RegisteredRegion.cpp
            src/internal/LocalRegion.cpp
            src/internal/RemoteRegion.cpp
            src/internal/Address.cpp
            src/internal/AddressVector.cpp
            src/internal/Completion.cpp
            src/internal/CompletionQueue.cpp
            src/internal/Event.cpp
            src/internal/EventQueue.cpp
            src/internal/Endpoint.cpp
    )
target_link_libraries(mxl-fabrics-objects
        PRIVATE
            mxl-internal-headers
            mxl-fabrics-headers
            PkgConfig::libfabric
)
set_target_properties(mxl-fabrics-objects
        PROPERTIES
            POSITION_INDEPENDENT_CODE ${MXL_POSITION_INDEPENDENT_CODE}
            C_EXTENSIONS              OFF
            CXX_EXTENSIONS            OFF
    )

# Enable LTO/IPO on target if enabled and supported
mxl_enable_target_ipo(mxl-fabrics-objects)

add_library(mxl-fabrics)

target_sources(mxl-fabrics
        PRIVATE
            $<TARGET_OBJECTS:mxl-fabrics-objects>
)

set_target_properties(mxl-fabrics
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

target_link_libraries(mxl-fabrics
        PUBLIC
            mxl
            mxl-fabrics-headers
            PkgConfig::libfabric
        PRIVATE
            spdlog::spdlog
            fmt::fmt
      )

# Add common linker options
mxl_add_common_target_link_options(mxl-fabrics PRIVATE)
# Enable LTO/IPO on target if enabled and supported
mxl_enable_target_ipo(mxl-fabrics)

add_library(${PROJECT_NAME}::mxl-fabrics ALIAS mxl-fabrics)


include(CMakePackageConfigHelpers)

set(MXL_FABRICS_CMAKE_CONFIG_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

install(TARGETS mxl-fabrics EXPORT mxl-fabrics-targets
        COMPONENT ${PROJECT_NAME}-lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

# This makes the project importable from the install directory
install(EXPORT mxl-fabrics-targets
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${MXL_FABRICS_CMAKE_CONFIG_DESTINATION}
        COMPONENT ${PROJECT_NAME}-dev
    )


# Generate the config file and put it into the build directory.
configure_package_config_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/mxl-fabrics-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/mxl-fabrics-config.cmake
        INSTALL_DESTINATION ${MXL_FABRICS_CMAKE_CONFIG_DESTINATION}
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

# Generate the version file and put it into the build directory.
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/mxl-fabrics-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

# Generate a pkg-config file
set(MXL_PKGCONFIG_PREFIX       "${CMAKE_INSTALL_PREFIX}")
set(MXL_PKGCONFIG_LIBDIR       "${CMAKE_INSTALL_FULL_LIBDIR}")
set(MXL_PKGCONFIG_INCLUDEDIR   "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
set(MXL_PKGCONFIG_EXTRA_CFLAGS "")
configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/libmxl-fabrics.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/libmxl-fabrics.pc
        @ONLY
    )
# Test-only .pc file in the build dir for pre-install test.
if (BUILD_TESTS)
    set(MXL_PKGCONFIG_PREFIX       "${CMAKE_CURRENT_BINARY_DIR}")
    set(MXL_PKGCONFIG_LIBDIR       "${CMAKE_CURRENT_BINARY_DIR}")
    set(MXL_PKGCONFIG_INCLUDEDIR   "${CMAKE_SOURCE_DIR}/lib/include")
    set(MXL_PKGCONFIG_EXTRA_CFLAGS "-I${CMAKE_SOURCE_DIR}/lib/fabrics/include")
    
    set(MXL_PKGCONFIG_TEST_DIR  "${CMAKE_BINARY_DIR}/pkgconfig_test")
    file(MAKE_DIRECTORY "${MXL_PKGCONFIG_TEST_DIR}")

    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/libmxl-fabrics.pc.in
        ${MXL_PKGCONFIG_TEST_DIR}/libmxl-fabrics.pc
        @ONLY
    )
endif()


# Install the generated config and version files.
install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/mxl-fabrics-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/mxl-fabrics-config-version.cmake
            DESTINATION ${MXL_FABRICS_CMAKE_CONFIG_DESTINATION}
        COMPONENT ${PROJECT_NAME}-dev
    )

# Install the generated pkg-config file
install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/libmxl-fabrics.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        COMPONENT ${PROJECT_NAME}-dev
    )

