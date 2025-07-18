# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer IMPORTED_TARGET gstreamer-1.0)
pkg_search_module(gstreamer-app IMPORTED_TARGET gstreamer-app-1.0)
pkg_search_module(gstreamer-video IMPORTED_TARGET gstreamer-video-1.0)

if (gstreamer_FOUND)
    if (NOT TARGET CLI11::CLI11)
        find_package(CLI11 CONFIG REQUIRED)
    endif ()

    if (NOT TARGET stduuid)
        find_package(stduuid CONFIG REQUIRED)
    endif ()

    if (NOT TARGET spdlog::spdlog)
        find_package(spdlog CONFIG REQUIRED)
    endif ()


    add_executable(mxl-gst-videotestsrc)
    target_compile_features(mxl-gst-videotestsrc
            PRIVATE
                cxx_std_20
        )
    set_target_properties(mxl-gst-videotestsrc
            PROPERTIES
                POSITION_INDEPENDENT_CODE    ON
                VISIBILITY_INLINES_HIDDEN    ON
                C_VISIBILITY_PRESET          hidden
                CXX_VISIBILITY_PRESET        hidden
                C_EXTENSIONS                 OFF
                CXX_EXTENSIONS               OFF
        )
    target_sources(mxl-gst-videotestsrc
            PRIVATE
                videotestsrc.cpp
                ../../lib/src/internal/FlowParser.cpp
                ../../lib/src/internal/PathUtils.cpp
            )

    target_link_libraries(mxl-gst-videotestsrc
            PRIVATE
                mxl
                stduuid
                CLI11::CLI11
                spdlog::spdlog
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-video
            )


    add_executable(mxl-gst-videosink)
    target_compile_features(mxl-gst-videosink
            PRIVATE
                cxx_std_20
        )
    set_target_properties(mxl-gst-videosink
            PROPERTIES
                POSITION_INDEPENDENT_CODE    ON
                VISIBILITY_INLINES_HIDDEN    ON
                C_VISIBILITY_PRESET          hidden
                CXX_VISIBILITY_PRESET        hidden
                C_EXTENSIONS                 OFF
                CXX_EXTENSIONS               OFF
        )
    target_sources(mxl-gst-videosink
            PRIVATE
                videosink.cpp
                ../../lib/src/internal/FlowParser.cpp
                ../../lib/src/internal/PathUtils.cpp
            )

    target_link_libraries(mxl-gst-videosink
            PRIVATE
                mxl
                stduuid
                CLI11::CLI11
                spdlog::spdlog
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-video
        )

    # Not really a fan of having this relative to the binary,
    # we should leave this at the default.
    set_target_properties(mxl-gst-videosink mxl-gst-videotestsrc
            PROPERTIES
                INSTALL_RPATH "$ORIGIN/../lib"
        )

    # Install targets
    install(TARGETS mxl-gst-videosink mxl-gst-videotestsrc
            COMPONENT ${PROJECT_NAME}-tools
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
endif()