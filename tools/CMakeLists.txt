# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

#
# MXL Tools - Demonstration applications and utilities
#
# This directory contains tools that demonstrate MXL SDK features:
#
#   mxl-info: Flow inspection and management utility
#     - List all flows in a domain
#     - Display detailed flow information
#     - Garbage collect inactive flows
#     - Command-line and URI-based addressing
#
#   mxl-gst: GStreamer integration tools
#     - mxl-gst-testsrc: Generate synthetic video/audio test patterns to MXL flows
#     - mxl-gst-sink: Read from MXL flows and play via GStreamer
#     - mxl-gst-looping-filesrc: Decode media files and write to MXL flows
#
#   mxl-fabrics-demo: RDMA-based network transport (optional, requires libfabric)
#     - Demonstrates zero-copy fabric transport of MXL flows
#     - Supports TCP, InfiniBand Verbs, AWS EFA, shared memory
#     - Target (receiver) and Initiator (sender) modes
#

add_subdirectory(mxl-info)
add_subdirectory(mxl-gst)

# Fabrics demo is only built if libfabric/OFI support is enabled
if (MXL_ENABLE_FABRICS_OFI)
    add_subdirectory(mxl-fabrics-demo)
endif ()
