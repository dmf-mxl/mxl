# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

#
# mxl-info - MXL flow inspection and management utility
#
# This tool provides command-line access to MXL domain inspection:
#   - List all flows with labels and group hints
#   - Display detailed flow configuration and runtime state
#   - Show latency and buffer utilization
#   - Perform garbage collection of inactive flows
#   - Support both traditional CLI options and MXL URI format
#
# Usage:
#   mxl-info -d /tmp/mxl-domain --list
#   mxl-info -d /tmp/mxl-domain -f <flow-uuid>
#   mxl-info mxl:///tmp/mxl-domain?id=<flow-uuid>
#   mxl-info -d /tmp/mxl-domain --garbage-collect
#
# Dependencies:
#   - CLI11: Command-line argument parsing
#   - stduuid: UUID parsing and formatting
#   - fmt: String formatting and terminal colors
#   - simple_uri_parser: MXL URI parsing
#

include(GNUInstallDirs)
include(FetchContent)

# Fetch simple URI parser for mxl:// URI support
FetchContent_Declare(
    simple_uri_parser
    GIT_REPOSITORY https://github.com/jholloc/simple-uri-parser.git
    GIT_TAG        v1.0.0
)

FetchContent_MakeAvailable(simple_uri_parser)
add_library(simple_uri_parser INTERFACE)

target_include_directories(simple_uri_parser
    INTERFACE
        ${simple_uri_parser_SOURCE_DIR}
)


# Create mxl-info executable
add_executable(mxl-info)
target_compile_features(mxl-info
        PRIVATE
            cxx_std_20  # Requires C++20 for structured bindings, concepts
    )
set_target_properties(mxl-info
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON     # Enable PIE for security
            VISIBILITY_INLINES_HIDDEN    ON     # Hide inline symbols
            C_VISIBILITY_PRESET          hidden # Default to hidden visibility
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF    # Disable GNU extensions
            CXX_EXTENSIONS               OFF
    )
target_sources(mxl-info
        PRIVATE
            main.cpp  # Flow inspection implementation
    )


# Find required dependencies
if (NOT TARGET CLI11::CLI11)
    find_package(CLI11 CONFIG REQUIRED)      # Command-line parsing
endif ()

if (NOT TARGET stduuid)
    find_package(stduuid CONFIG REQUIRED)    # UUID handling
endif ()

if (NOT TARGET fmt::fmt)
    find_package(fmt CONFIG REQUIRED)        # Formatting and colors
endif()

# Link all dependencies
target_link_libraries(mxl-info
        PRIVATE
            simple_uri_parser  # MXL URI parsing
            stduuid            # Flow ID parsing
            fmt::fmt           # Terminal output formatting
            mxl                # MXL SDK core library
            CLI11::CLI11       # Command-line interface
    )

# Set runtime library search path (for finding libmxl.so at runtime)
# Uses $ORIGIN-relative path so tool can be relocated
set_target_properties(mxl-info
        PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
    )

# Install mxl-info to bin directory
install(TARGETS mxl-info
        COMPONENT ${PROJECT_NAME}-tools
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
