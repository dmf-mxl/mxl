# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

add_executable(mxl-fabrics-info)
target_compile_features(mxl-fabrics-info
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl-fabrics-info
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )
target_sources(mxl-fabrics-info
        PRIVATE
            main.cpp
    )

if (NOT TARGET CLI11::CLI11)
    find_package(CLI11 CONFIG REQUIRED)
endif ()

if (NOT TARGET fmt::fmt)
    find_package(fmt CONFIG REQUIRED)
endif()

if (NOT TARGET picojson::picojson)
    find_package(picojson REQUIRED)
endif()

target_link_libraries(mxl-fabrics-info
        PRIVATE
            mxl
            mxl-fabrics
            fmt::fmt
            CLI11::CLI11
            picojson::picojson
    )

set_target_properties(mxl-fabrics-info
        PROPERTIES
            INSTALL_RPATH "${MXL_TOOLS_INSTALL_RPATH}"
    )

# Install targets
install(TARGETS mxl-fabrics-info
        COMPONENT ${PROJECT_NAME}-tools
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
