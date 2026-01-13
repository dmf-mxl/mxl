# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

# Allow version in project definition
cmake_policy(SET CMP0048 NEW)
# Only interpret if arguments as variables or keywords when unquoted.
cmake_policy(SET CMP0054 NEW)
# Allow visibility definitions
cmake_policy(SET CMP0063 NEW)
# Interpret target_sources paths as relative to the current source dir
cmake_policy(SET CMP0076 NEW)
# Set the timestamps of all extracted contents to the time of the extraction
cmake_policy(SET CMP0135 NEW)

option(MXL_ENABLE_FABRICS_OFI "Enable building the fabrics library with libfabric" OFF)

# Build type. Used as a build suffix or in the operating system package file name.  Currently manually set but could be automated using branch names, etc.
# One of : dev, beta, rc, "" (empty for final releases)
set(MXL_BUILD_TYPE "rc")
set(MXL_BUILD_SUFFIX "-${MXL_BUILD_TYPE}")

set(mxl_VERSION 1.0.0)

if(DEFINED MXL_BUILD_NUMBER)
    string(APPEND mxl_VERSION ".${MXL_BUILD_NUMBER}")
else()
    string(APPEND mxl_VERSION ".0")
endif()

find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(GIT_HASH "unknown")
endif()


option(BUILD_DOCS "Build the docs" ON)
option(BUILD_TESTS "Build the tests" ON)
option(BUILD_TOOLS "Build the tools" ON)

project(mxl
    VERSION ${mxl_VERSION}
    LANGUAGES CXX C
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")


#-------------------------------------------------------------------------------
# Determine optional IPO support and allow enabling IPO on a per target basis.
#-------------------------------------------------------------------------------
option(MXL_ENABLE_IPO "Enable LTO/IPO on the library targets." OFF)

set(MXL_IPO_SUPPORTED OFF)
if(MXL_ENABLE_IPO)
    include(CheckIPOSupported)

    check_ipo_supported(RESULT MXL_IPO_SUPPORTED OUTPUT output LANGUAGES C CXX)
    if(MXL_IPO_SUPPORTED)
        message(STATUS "Enabling LTO/IPO support.")
    else()
        message(WARNING "LTO/IPO is not supported: ${output}")
    endif()
endif()

if (MXL_IPO_SUPPORTED)
    function(mxl_enable_target_ipo target)
        message(STATUS "Enabling LTO/IPO on target: ${target}")
        set_target_properties(${target}
                PROPERTIES
                    INTERPROCEDURAL_OPTIMIZATION ON
            )
    endfunction()
else ()
    function(mxl_enable_target_ipo target)
    endfunction()
endif ()


find_program(ccache_executable ccache)

if(ccache_executable)
    message(STATUS "Using ccache from '${ccache_executable}'")
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${ccache_executable})
endif()

# Enable testing for the project
enable_testing()

# Set macOS specific configuration
if(APPLE)
    # Add the official GStreamer framework paths to the PKG_CONFIG_PATH
    set(ENV{PKG_CONFIG_PATH} "/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

# Default build is shared libraries.
# Use '-DBUILD_SHARED_LIBS=OFF' for static build.
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

# Default PIC ON for static builds.
# Use -DMXL_ENABLE_PIC=OFF to disable PIC
option(MXL_ENABLE_PIC "Enable PIC for static libraries" ON)

# Policy: shared always PIC, static set to MXL_ENABLE_PIC.
if(NOT BUILD_SHARED_LIBS)
    set(MXL_POSITION_INDEPENDENT_CODE ${MXL_ENABLE_PIC})
else()
    set(MXL_POSITION_INDEPENDENT_CODE ON)
endif()

#-------------------------------------------------------------------------------
# Helper to conditionally add flags supported by the current compiler
#-------------------------------------------------------------------------------
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

function(mxl_check_and_add_flag flag)
    string(REGEX REPLACE "^-+" "" var "${flag}")
    string(MAKE_C_IDENTIFIER "${var}" var)
    string(TOUPPER "${var}" var)

    set(c_var "MXL_C_COMPILER_SUPPORTS_${var}")
    set(cxx_var "MXL_CXX_COMPILER_SUPPORTS_${var}")

    set(lang_list "")
    check_c_compiler_flag("${flag}" ${c_var})
    if (${c_var})
        list(APPEND lang_list C)
    endif ()

    check_cxx_compiler_flag("${flag}" ${cxx_var})
    if (${cxx_var})
        list(APPEND lang_list CXX)
    endif ()

    list(JOIN lang_list "," lang_list_string)
    if (NOT "${lang_list_string}" STREQUAL "")
        add_compile_options($<$<COMPILE_LANGUAGE:${lang_list_string}>:${flag}>)
    endif ()
endfunction()

#-------------------------------------------------------------------------------
# Globally add warning flags
#-------------------------------------------------------------------------------
mxl_check_and_add_flag("-Wall")
mxl_check_and_add_flag("-Wextra")
mxl_check_and_add_flag("-Wunreachable-code")
mxl_check_and_add_flag("-Wpedantic")
mxl_check_and_add_flag("-Wno-c++23-attribute-extensions")


#-------------------------------------------------------------------------------
# Allow forcing a certain optimization level regardless of what has been
# provided through the build type, the environment, the toolchain file or
# whatever.
#-------------------------------------------------------------------------------
set(MXL_FORCE_OPTIMIZATION_LEVEL OFF CACHE STRING "Force a sepcific optimization level: Can be one of 0, 1, 2, 3, fast or s.")

if (MXL_FORCE_OPTIMIZATION_LEVEL)
    check_cxx_compiler_flag("-O${MXL_FORCE_OPTIMIZATION_LEVEL}" MXL_CXX_COMPILER_SUPPORTS_OPTIMIZATION_LEVEL)
    if (MXL_CXX_COMPILER_SUPPORTS_OPTIMIZATION_LEVEL)
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-O${MXL_FORCE_OPTIMIZATION_LEVEL}>)
    else()
        message(ERROR "The compiler does not recognize the specified optimization.")
    endif()
endif()


#-------------------------------------------------------------------------------
# If specified try to use libc++ instead of libstdcxx globally for all targets.
#-------------------------------------------------------------------------------
set(MXL_USE_LIBCPP OFF CACHE STRING "Explicitly use libc++ instead of the standard library provided by the system.")

if (MXL_USE_LIBCPP)
    function(mxl_check_libcpp outvar)
        string(APPEND CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")
        check_cxx_compiler_flag("-stdlib=libc++" ${outvar})
        set(${outvar} ${${outvar}} PARENT_SCOPE)
    endfunction()

    mxl_check_libcpp(MXL_HAS_LIBCPP)
    if (MXL_HAS_LIBCPP)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
        add_link_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
    else()
        message(WARNING "Could not switch to libc++, because it is notusable on the current platform.")
    endif()
endif()


add_subdirectory(lib)
add_subdirectory(utils)
if (BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(BUILD_DOCS)
    find_package(Doxygen)

    if(DOXYGEN_FOUND)
        include(FetchContent)

        FetchContent_Declare(
            doxygen-awesome-css
            URL https://github.com/jothepro/doxygen-awesome-css/archive/refs/heads/main.zip
        )
        FetchContent_MakeAvailable(doxygen-awesome-css)

        # Save the location the files were cloned into
        # This allows us to get the path to doxygen-awesome.css
        FetchContent_GetProperties(doxygen-awesome-css SOURCE_DIR AWESOME_CSS_DIR)

        # Generate the Doxyfile
        set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
        set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

        set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")

        add_custom_target(doc
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            BYPRODUCTS "${DOXYGEN_OUTPUT_DIR}/html/index.html"
            VERBATIM
        )

        install(DIRECTORY "${CMAKE_BINARY_DIR}/docs/html"
            DESTINATION share/doc/mxl
            FILES_MATCHING PATTERN "*")
    endif()
endif()

# CPack Configuration
set(CPACK_PACKAGE_NAME "dmfmxl-dev")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "EBU DMF Media eXchange Layer SDK development package")
set(CPACK_PACKAGE_VENDOR "EBU.ch")
set(CPACK_PACKAGE_CONTACT "DMF MXL <mxl@ebu.ch>")

if(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" OS_RELEASE_CONTENTS)

    set(IS_DEBIAN FALSE)
    set(IS_RPM FALSE)

    foreach(LINE IN LISTS OS_RELEASE_CONTENTS)
        if(LINE MATCHES "^ID=(.*)")
            set(DISTRO_ID "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    if(DISTRO_ID MATCHES "ubuntu|debian")
        set(IS_DEBIAN TRUE)
    elseif(DISTRO_ID MATCHES "alma|amzn|rocky|rhel|fedora|centos")
        set(IS_RPM TRUE)
    endif()

    if(IS_DEBIAN)
        set(CPACK_GENERATOR "DEB")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "gstreamer1.0-x, gstreamer1.0-plugins-good")
        set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

        find_program(DPKG_EXECUTABLE dpkg)

        if(DPKG_EXECUTABLE)
            execute_process(
                COMMAND ${DPKG_EXECUTABLE} --print-architecture
                OUTPUT_VARIABLE DEB_ARCH
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            message(FATAL_ERROR "dpkg not found — cannot determine Debian architecture")
        endif()
        set(CPACK_PACKAGE_ARCHITECTURE "${DEB_ARCH}")
        set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}~${MXL_BUILD_TYPE}+g${GIT_HASH}-1_${CPACK_PACKAGE_ARCHITECTURE}")

    elseif(IS_RPM)
        set(CPACK_GENERATOR "RPM")
        set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
        set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
        # Let RPM auto-detect arch (x86_64, aarch64, etc.)
        #unset(CPACK_RPM_PACKAGE_ARCHITECTURE)
        set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

        # Not adding gstreamer as a dependency since some rpm based platforms do not provide the required packages (amazon linux 2023)
        # set(CPACK_RPM_PACKAGE_REQUIRES "")

        string(TOLOWER "${MXL_BUILD_TYPE}" _bt)
        set(_rpm_release "1")

        if(_bt STREQUAL "")
            # final release
            set(_rpm_release "1")
        else()
            set(_rpm_release "0.1.${_bt}.g${GIT_HASH}")
        endif()
        set(CPACK_RPM_PACKAGE_RELEASE "${_rpm_release}%{?dist}")
        set(CPACK_RPM_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${_rpm_release}.${CMAKE_SYSTEM_PROCESSOR}.rpm")

    endif()
endif()

# Include CPack
include(CPack)
