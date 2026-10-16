# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

if(NOT TARGET CLI11::CLI11)
    find_package(CLI11 CONFIG REQUIRED)
endif()

if(NOT TARGET fmt::fmt)
    find_package(fmt CONFIG REQUIRED)
endif()

if(NOT TARGET ada::ada)
    find_package(ada CONFIG REQUIRED)
endif()

# Code shared by the target and the initiator.
add_library(mxl-fabrics-demo-common OBJECT common.cpp)
target_link_libraries(mxl-fabrics-demo-common
    PUBLIC
        mxl
        mxl-fabrics
        fmt::fmt
        CLI11::CLI11
    PRIVATE
        ada::ada)

add_executable(mxl-fabrics-demo-target target.cpp)
target_link_libraries(mxl-fabrics-demo-target 
    PRIVATE 
        mxl-fabrics-demo-common)

add_executable(mxl-fabrics-demo-initiator initiator.cpp)
target_link_libraries(mxl-fabrics-demo-initiator 
    PRIVATE
        mxl-fabrics-demo-common)

set_target_properties(
    mxl-fabrics-demo-common
    mxl-fabrics-demo-target
    mxl-fabrics-demo-initiator
        PROPERTIES
            C_EXTENSIONS   OFF
            CXX_EXTENSIONS OFF
            C_STANDARD     ${MXL_C_STANDARD}
            CXX_STANDARD   ${MXL_CXX_STANDARD}
            INSTALL_RPATH  "${MXL_TOOLS_INSTALL_RPATH}"
    )

install(TARGETS mxl-fabrics-demo-target mxl-fabrics-demo-initiator
        RUNTIME DESTINATION bin)
