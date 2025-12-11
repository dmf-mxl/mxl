# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

message ("-- Building fabrics library with libfabric/ofi")

find_package(PkgConfig REQUIRED)

pkg_check_modules(libfabric REQUIRED IMPORTED_TARGET libfabric)

add_library(mxl-fabrics-objects OBJECT)
target_compile_features(mxl-fabrics-objects
        PRIVATE
            cxx_std_20
    )
target_sources(mxl-fabrics-objects
        PRIVATE
            src/fabrics.cpp
            src/internal/Address.cpp
            src/internal/AddressVector.cpp
            src/internal/Completion.cpp
            src/internal/CompletionQueue.cpp
            src/internal/DataLayout.cpp
            src/internal/Domain.cpp
            src/internal/Endpoint.cpp
            src/internal/Event.cpp
            src/internal/EventQueue.cpp
            src/internal/Exception.cpp
            src/internal/FILogging.cpp
            src/internal/Fabric.cpp
            src/internal/FabricInfo.cpp
            src/internal/FabricInstance.cpp
            src/internal/FabricVersion.cpp
            src/internal/ImmData.cpp
            src/internal/Initiator.cpp
            src/internal/LocalRegion.cpp
            src/internal/MemoryRegion.cpp
            src/internal/PassiveEndpoint.cpp
            src/internal/Protocol.cpp
            src/internal/ProtocolEgressRMA.cpp
            src/internal/ProtocolIngressRMA.cpp
            src/internal/Provider.cpp
            src/internal/RCInitiator.cpp
            src/internal/RCTarget.cpp
            src/internal/RDMInitiator.cpp
            src/internal/RDMTarget.cpp
            src/internal/Region.cpp
            src/internal/RegisteredRegion.cpp
            src/internal/RemoteRegion.cpp
            src/internal/GrainSlices.cpp
            src/internal/Target.cpp
            src/internal/TargetInfo.cpp
    )
target_link_libraries(mxl-fabrics-objects
        PUBLIC
            mxl
            mxl-fabrics-headers
            PkgConfig::libfabric
        PRIVATE
            mxl-internal-headers
    )
set_target_properties(mxl-fabrics-objects
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_EXTENSIONS              OFF
            CXX_EXTENSIONS            OFF
    )

add_library(mxl-fabrics SHARED)
target_link_libraries(mxl-fabrics
        PRIVATE
            mxl-fabrics-objects
    )
set_target_properties(mxl-fabrics-objects
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            VISIBILITY_INLINES_HIDDEN ON
            C_VISIBILITY_PRESET       hidden
            CXX_VISIBILITY_PRESET     hidden
            C_EXTENSIONS              OFF
            CXX_EXTENSIONS            OFF
            VERSION                   "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            SOVERSION                 "${PROJECT_VERSION_MAJOR}"
    )

target_link_options(mxl-fabrics
        PRIVATE
            "-Wl,--exclude-libs,ALL"
    )
target_link_libraries(mxl-fabrics
        PUBLIC
            mxl
            mxl-fabrics-headers
            PkgConfig::libfabric
    )

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
configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/libmxl-fabrics.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/libmxl-fabrics.pc
        @ONLY
    )


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

