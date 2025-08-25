# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
#
# SPDX-License-Identifier: Apache-2.0

find_package(CLI11 CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Add the test executable
add_executable(mxl-fabrics-demo
    demo.cpp
)

target_link_libraries(mxl-fabrics-demo PRIVATE
    mxl
    mxl-fabrics
    stduuid
    CLI11::CLI11
    spdlog::spdlog
)

set_target_properties(mxl-fabrics-demo PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
)

# Install targets
install(TARGETS mxl-fabrics-demo
    RUNTIME DESTINATION bin
)
