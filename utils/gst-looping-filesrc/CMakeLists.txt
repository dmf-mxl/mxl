# SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.22)

project(gst-looping-filesrc LANGUAGES C CXX)

find_package(PkgConfig)

pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-base-1.0)

add_library(looping_filesrc SHARED
    gst-looping-filesrc.cpp
    )

if(MSVC)
    # Force to always compile with W4
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
	string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(looping_filesrc PRIVATE
	-Wall
	-Wno-long-long
	-pedantic
	)
endif()

target_include_directories(looping_filesrc PUBLIC
    .
    )

set_target_properties(looping_filesrc PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF
    )

target_link_libraries(looping_filesrc PUBLIC
    PkgConfig::GST
    )

set_target_properties(PROPERTIES PUBLIC_HEADER gst-looping-filesrc.h)

set(GSTREAMER_PLUGIN_PATH /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/gstreamer-1.0/ CACHE PATH "Where to install the plugin")
message(STATUS "GSTREAMER_PLUGIN_PATH=\"${GSTREAMER_PLUGIN_PATH}\"")

install(
    TARGETS looping_filesrc
    DESTINATION ${GSTREAMER_PLUGIN_PATH}
    PUBLIC_HEADER
    DESTINATION ${GSTREAMER_PLUGIN_PATH}/include)
