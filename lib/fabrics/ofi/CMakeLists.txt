message ("-- Building fabrics library with libfabric/ofi")

add_library(mxl-fabrics SHARED)

target_sources(mxl-fabrics
        PRIVATE
            src/fabrics.cpp
    )

target_link_libraries(mxl-fabrics
        PUBLIC
            mxl-fabrics-headers
            mxl-internal
        PRIVATE
            mxl-headers
    )

target_compile_features(mxl-fabrics
        PRIVATE
            cxx_std_20
    )

set_target_properties(mxl-fabrics
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

add_library(${PROJECT_NAME}::mxl-fabrics ALIAS mxl-fabrics)

install(TARGETS mxl-fabrics EXPORT ${PROJECT_NAME}-targets
        COMPONENT ${PROJECT_NAME}-lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

include(CMakePackageConfigHelpers)

# Generate a pkg-config file
configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/libmxl-fabrics.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/libmxl-fabrics.pc
        @ONLY
    )

# Install the generated pkg-config file
install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/libmxl-fabrics.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        COMPONENT ${PROJECT_NAME}-dev
    )

