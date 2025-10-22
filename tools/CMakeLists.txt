# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

add_subdirectory(mxl-info)
add_subdirectory(mxl-gst)

if (MXL_FABRICS_OFI)
    add_subdirectory(mxl-fabrics-demo)
endif()
