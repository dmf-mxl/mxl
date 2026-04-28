# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

add_executable(mxl-data-probe)
target_compile_features(mxl-data-probe
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl-data-probe
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )
target_sources(mxl-data-probe
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
endif ()

if (NOT TARGET ada::ada)
    find_package(ada CONFIG REQUIRED)
endif ()

target_link_libraries(mxl-data-probe
        PRIVATE
            mxl
            ada::ada
            CLI11::CLI11
            fmt::fmt
            picojson::picojson
    )

set_target_properties(mxl-data-probe
        PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
    )

install(TARGETS mxl-data-probe
        COMPONENT ${PROJECT_NAME}-tools
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
