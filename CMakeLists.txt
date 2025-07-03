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

set(mxl_VERSION 0.6.1)

if(DEFINED MXL_BUILD_NUMBER)
    string(APPEND mxl_VERSION ".${MXL_BUILD_NUMBER}")
else()
    string(APPEND mxl_VERSION ".0")
endif()

project(mxl
    VERSION ${mxl_VERSION}
    LANGUAGES CXX C
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

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

    # Configure threads
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()

add_subdirectory(lib)
add_subdirectory(tools)


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
    elseif(DISTRO_ID MATCHES "amzn|rocky|rhel|fedora|centos")
        set(IS_RPM TRUE)
    endif()

    if(IS_DEBIAN)
        set(CPACK_GENERATOR "DEB")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "gstreamer1.0-x, gstreamer1.0-plugins-good")
        set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    elseif(IS_RPM)
        set(CPACK_GENERATOR "RPM")

        # Not adding gstreamer as a dependency since some rpm based platforms do not provide the required packages (amazon linux 2023)
        # set(CPACK_RPM_PACKAGE_REQUIRES "")
    endif()
endif()

# CPack Configuration
set(CPACK_PACKAGE_NAME "dmfmxl-dev")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "EBU DMF Media Exchange Layer SDK development package")
set(CPACK_PACKAGE_VENDOR "EBU.ch")
set(CPACK_PACKAGE_CONTACT "DMF MXL <mxl@ebu.ch>")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")

# Include CPack
include(CPack)