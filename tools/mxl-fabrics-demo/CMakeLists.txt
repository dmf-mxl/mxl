# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
#
# SPDX-License-Identifier: Apache-2.0

#
# mxl-fabrics-demo - RDMA-based network transport demonstration
#
# This tool demonstrates MXL's fabrics layer for zero-copy network transport:
#
#   Target mode (receiver):
#     - Creates an MXL flow writer
#     - Listens for RDMA connections
#     - Receives grains via RDMA from remote initiators
#     - Commits grains to local flow
#
#   Initiator mode (sender):
#     - Opens an MXL flow reader
#     - Connects to remote target via RDMA
#     - Reads grains locally and transmits via RDMA
#
# Supported fabrics:
#   - TCP: Ethernet-based (no special hardware required)
#   - Verbs: InfiniBand and RoCE (libibverbs)
#   - EFA: AWS Elastic Fabric Adapter
#   - SHM: Shared memory (same-host only)
#
# This tool requires:
#   - MXL_ENABLE_FABRICS_OFI=ON at build time
#   - libfabric (OFI) installed
#   - Appropriate fabric provider available (tcp, verbs, efa)
#
# Usage:
#   # Start target (receiver):
#   ./mxl-fabrics-demo -d /tmp/domain -f flow.json \
#                      --node 2.2.2.2 --service 1234 --provider verbs
#
#   # Start initiator (sender):
#   ./mxl-fabrics-demo -i -d /tmp/domain -f <flow-uuid> \
#                      --node 1.1.1.1 --service 1234 --provider verbs \
#                      --target-info <base64-string-from-target>
#

# Find required dependencies
find_package(CLI11 CONFIG REQUIRED)      # Command-line parsing
find_package(stduuid CONFIG REQUIRED)    # UUID handling
find_package(spdlog CONFIG REQUIRED)     # Structured logging

# Create mxl-fabrics-demo executable
add_executable(mxl-fabrics-demo)
target_compile_features(mxl-fabrics-demo
        PRIVATE
            cxx_std_20  # Requires C++20
    )
target_sources(mxl-fabrics-demo
        PRIVATE
            demo.cpp    # Initiator and target implementation
    )
target_link_libraries(mxl-fabrics-demo
        PRIVATE
            mxl                      # MXL SDK core
            mxl-fabrics              # MXL fabrics layer (libfabric wrapper)
            mxl-internal-headers     # Internal MXL headers
            mxl-common               # Common utilities
            stduuid                  # Flow ID handling
            CLI11::CLI11             # CLI
            spdlog::spdlog           # Logging
    )

# Set runtime library search path
set_target_properties(mxl-fabrics-demo PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
    )

# Install to bin directory
install(TARGETS mxl-fabrics-demo
        RUNTIME DESTINATION bin
    )
