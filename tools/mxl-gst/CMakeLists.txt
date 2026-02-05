# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer IMPORTED_TARGET gstreamer-1.0)
pkg_search_module(gstreamer-app IMPORTED_TARGET gstreamer-app-1.0)
pkg_search_module(gstreamer-audio IMPORTED_TARGET gstreamer-audio-1.0)
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

    if (NOT TARGET picojson::picojson)
        find_package(picojson REQUIRED)
    endif ()

    add_executable(mxl-gst-testsrc)
    target_compile_features(mxl-gst-testsrc
            PRIVATE
                cxx_std_20
        )
    set_target_properties(mxl-gst-testsrc
            PROPERTIES
                POSITION_INDEPENDENT_CODE    ON
                VISIBILITY_INLINES_HIDDEN    ON
                C_VISIBILITY_PRESET          hidden
                CXX_VISIBILITY_PRESET        hidden
                C_EXTENSIONS                 OFF
                CXX_EXTENSIONS               OFF
        )
    target_sources(mxl-gst-testsrc
            PRIVATE
                testsrc.cpp
        )

    target_link_libraries(mxl-gst-testsrc
            PRIVATE
                mxl
                stduuid
                fmt::fmt
                picojson::picojson
                CLI11::CLI11
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-video
            )


    add_executable(mxl-gst-sink)
    target_compile_features(mxl-gst-sink
            PRIVATE
                cxx_std_20
        )
    set_target_properties(mxl-gst-sink
            PROPERTIES
                POSITION_INDEPENDENT_CODE   ON
                VISIBILITY_INLINES_HIDDEN   ON
                C_VISIBILITY_PRESET         hidden
                CXX_VISIBILITY_PRESET       hidden
                C_EXTENSIONS                OFF
                CXX_EXTENSIONS              OFF
        )
    target_sources(mxl-gst-sink
            PRIVATE
                sink.cpp
        )

    target_link_libraries(mxl-gst-sink
            PRIVATE
                mxl
                stduuid
                fmt::fmt
                picojson::picojson
                CLI11::CLI11
                spdlog::spdlog
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-audio
                PkgConfig::gstreamer-video
        )

    add_executable(mxl-gst-looping-filesrc)
    target_compile_features(mxl-gst-looping-filesrc
            PRIVATE
                cxx_std_20
        )
    set_target_properties(mxl-gst-looping-filesrc
            PROPERTIES
                POSITION_INDEPENDENT_CODE   ON
                VISIBILITY_INLINES_HIDDEN   ON
                C_VISIBILITY_PRESET         hidden
                CXX_VISIBILITY_PRESET       hidden
                C_EXTENSIONS                OFF
                CXX_EXTENSIONS              OFF
        )
    target_sources(mxl-gst-looping-filesrc
            PRIVATE
                looping_filesrc.cpp
        )

    target_link_libraries(mxl-gst-looping-filesrc
            PRIVATE
                mxl
                stduuid
                fmt::fmt
                picojson::picojson
                CLI11::CLI11
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-audio
                PkgConfig::gstreamer-video
        )

    # Install targets
    install(TARGETS mxl-gst-sink mxl-gst-testsrc mxl-gst-looping-filesrc
            COMPONENT ${PROJECT_NAME}-tools
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
endif()
