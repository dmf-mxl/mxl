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

if (NOT TARGET stduuid)
    find_package(stduuid CONFIG REQUIRED)
endif ()

if (NOT TARGET fmt::fmt)
    find_package(fmt CONFIG REQUIRED)
endif()

if (NOT TARGET picojson::picojson)
    find_package(picojson REQUIRED)
endif()

if (NOT TARGET ada::ada)
    find_package(ada CONFIG REQUIRED)
endif ()

target_link_libraries(mxl-info
        PRIVATE
            ada::ada
            stduuid
            fmt::fmt
            picojson::picojson
            mxl
            CLI11::CLI11
    )

set_target_properties(mxl-info
        PROPERTIES
            INSTALL_RPATH "${MXL_TOOLS_INSTALL_RPATH}"
    )

# Install targets
install(TARGETS mxl-info
        COMPONENT ${PROJECT_NAME}-tools
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
