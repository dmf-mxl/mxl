# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

add_executable(mxl-info)
target_compile_features(mxl-info
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl-info
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )
target_sources(mxl-info
        PRIVATE
            main.cpp
    )


if (NOT TARGET CLI11::CLI11)
    find_package(CLI11 CONFIG REQUIRED)
endif ()

target_link_libraries(mxl-info
        PRIVATE
            mxl
            mxl-internal-headers
            CLI11::CLI11
    )

# Not really a fan of having this relative to the binary, we should leave this
# at the default.
set_target_properties(mxl-info
        PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
    )

# Install targets
install(TARGETS mxl-info
        COMPONENT ${PROJECT_NAME}-tools
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
