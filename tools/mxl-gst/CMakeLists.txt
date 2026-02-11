# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

#
# mxl-gst - GStreamer integration tools for MXL
#
# This directory builds three GStreamer-based MXL demonstration tools:
#
#   mxl-gst-testsrc: Test pattern generator → MXL flows
#     - Generates synthetic video (videotestsrc) and audio (audiotestsrc)
#     - Writes to MXL flows in v210 and float32 formats
#     - Configurable patterns, overlays, and test tones
#
#   mxl-gst-sink: MXL flows → GStreamer playback
#     - Reads video and audio from MXL flows
#     - Renders via autovideosink and autoaudiosink
#     - Demonstrates zero-copy consumption with proper timing
#
#   mxl-gst-looping-filesrc: Media files → MXL flows
#     - Decodes any GStreamer-supported format (MP4, MPEG-TS, etc.)
#     - Writes decoded streams to MXL flows
#     - Creates NMOS flow descriptors automatically
#
# These tools are only built if GStreamer 1.0 is available on the system.
#
# Dependencies (via pkg-config):
#   - gstreamer-1.0: Core GStreamer framework
#   - gstreamer-app-1.0: appsrc/appsink elements
#   - gstreamer-audio-1.0: Audio format handling
#   - gstreamer-video-1.0: Video format handling
#

include(GNUInstallDirs)

# Find GStreamer via pkg-config
find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer IMPORTED_TARGET gstreamer-1.0)
pkg_search_module(gstreamer-app IMPORTED_TARGET gstreamer-app-1.0)
pkg_search_module(gstreamer-audio IMPORTED_TARGET gstreamer-audio-1.0)
pkg_search_module(gstreamer-video IMPORTED_TARGET gstreamer-video-1.0)

# Only build GStreamer tools if GStreamer is available
if (gstreamer_FOUND)
    # Find common dependencies for all GStreamer tools
    if (NOT TARGET CLI11::CLI11)
        find_package(CLI11 CONFIG REQUIRED)      # Command-line parsing
    endif ()

    if (NOT TARGET stduuid)
        find_package(stduuid CONFIG REQUIRED)    # UUID handling
    endif ()

    if (NOT TARGET spdlog::spdlog)
        find_package(spdlog CONFIG REQUIRED)     # Structured logging
    endif ()

    #
    # mxl-gst-testsrc: Generate synthetic test patterns and write to MXL flows
    #
    add_executable(mxl-gst-testsrc)
    target_compile_features(mxl-gst-testsrc
            PRIVATE
                cxx_std_20  # Requires C++20
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
                testsrc.cpp      # Test pattern generation and MXL writing
        )

    target_link_libraries(mxl-gst-testsrc
            PRIVATE
                mxl                          # MXL SDK
                stduuid                      # Flow ID generation
                fmt::fmt                     # Logging
                CLI11::CLI11                 # CLI
                PkgConfig::gstreamer         # GStreamer core
                PkgConfig::gstreamer-app     # appsink for pulling buffers
                PkgConfig::gstreamer-video   # Video format handling
            )

    #
    # mxl-gst-sink: Read from MXL flows and play via GStreamer
    #
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
                sink.cpp         # MXL reading and GStreamer playback
        )

    target_link_libraries(mxl-gst-sink
            PRIVATE
                mxl                          # MXL SDK
                stduuid                      # Flow ID parsing
                fmt::fmt                     # Logging
                CLI11::CLI11                 # CLI
                spdlog::spdlog               # Structured logging
                PkgConfig::gstreamer         # GStreamer core
                PkgConfig::gstreamer-app     # appsrc for pushing buffers
                PkgConfig::gstreamer-audio   # Audio format handling
                PkgConfig::gstreamer-video   # Video format handling
        )

    #
    # mxl-gst-looping-filesrc: Decode media files and write to MXL flows
    #
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
                looping_filesrc.cpp  # File decoding and MXL writing
        )

    target_link_libraries(mxl-gst-looping-filesrc
            PRIVATE
                mxl                          # MXL SDK
                stduuid                      # Flow ID generation
                fmt::fmt                     # Logging
                CLI11::CLI11                 # CLI
                PkgConfig::gstreamer         # GStreamer core
                PkgConfig::gstreamer-app     # appsink for pulling buffers
                PkgConfig::gstreamer-audio   # Audio format handling
                PkgConfig::gstreamer-video   # Video format handling
        )

    # Install all three GStreamer tools to bin directory
    install(TARGETS mxl-gst-sink mxl-gst-testsrc mxl-gst-looping-filesrc
            COMPONENT ${PROJECT_NAME}-tools
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
endif()  # gstreamer_FOUND
