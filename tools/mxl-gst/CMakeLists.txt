# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer IMPORTED_TARGET gstreamer-1.0)
pkg_search_module(gstreamer-app IMPORTED_TARGET gstreamer-app-1.0)
pkg_search_module(gstreamer-audio IMPORTED_TARGET gstreamer-audio-1.0)
pkg_search_module(gstreamer-video IMPORTED_TARGET gstreamer-video-1.0)

if (gstreamer_FOUND)
    if (NOT TARGET CLI11::CLI11)
        find_package(CLI11 CONFIG REQUIRED)
    endif ()

    if (NOT TARGET stduuid)
        find_package(stduuid CONFIG REQUIRED)
    endif ()

    if (NOT TARGET spdlog::spdlog)
        find_package(spdlog CONFIG REQUIRED)
    endif ()

    if (NOT TARGET picojson::picojson)
        find_package(picojson REQUIRED)
    endif ()

    add_executable(mxl-gst-testsrc)
    set_target_properties(mxl-gst-testsrc
            PROPERTIES
                POSITION_INDEPENDENT_CODE    ON
                VISIBILITY_INLINES_HIDDEN    ON
                C_VISIBILITY_PRESET          hidden
                CXX_VISIBILITY_PRESET        hidden
                C_EXTENSIONS                 OFF
                CXX_EXTENSIONS               OFF
                C_STANDARD                   ${MXL_C_STANDARD}
                CXX_STANDARD                 ${MXL_CXX_STANDARD}
        )
    target_sources(mxl-gst-testsrc
            PRIVATE
                testsrc.cpp
        )

    target_link_libraries(mxl-gst-testsrc
            PRIVATE
                mxl
                stduuid
                fmt::fmt
                picojson::picojson
                CLI11::CLI11
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-video
            )


    add_executable(mxl-gst-sink)
    set_target_properties(mxl-gst-sink
            PROPERTIES
                POSITION_INDEPENDENT_CODE   ON
                VISIBILITY_INLINES_HIDDEN   ON
                C_VISIBILITY_PRESET         hidden
                CXX_VISIBILITY_PRESET       hidden
                C_EXTENSIONS                OFF
                CXX_EXTENSIONS              OFF
                C_STANDARD                   ${MXL_C_STANDARD}
                CXX_STANDARD                 ${MXL_CXX_STANDARD}
        )
    target_sources(mxl-gst-sink
            PRIVATE
                sink.cpp
        )

    target_link_libraries(mxl-gst-sink
            PRIVATE
                mxl
                stduuid
                fmt::fmt
                picojson::picojson
                CLI11::CLI11
                spdlog::spdlog
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-audio
                PkgConfig::gstreamer-video
        )

    add_executable(mxl-gst-looping-filesrc)
    set_target_properties(mxl-gst-looping-filesrc
            PROPERTIES
                POSITION_INDEPENDENT_CODE   ON
                VISIBILITY_INLINES_HIDDEN   ON
                C_VISIBILITY_PRESET         hidden
                CXX_VISIBILITY_PRESET       hidden
                C_EXTENSIONS                OFF
                CXX_EXTENSIONS              OFF
                C_STANDARD                  ${MXL_C_STANDARD}
                CXX_STANDARD                ${MXL_CXX_STANDARD}
        )
    target_sources(mxl-gst-looping-filesrc
            PRIVATE
                looping_filesrc.cpp
        )

    target_link_libraries(mxl-gst-looping-filesrc
            PRIVATE
                mxl
                stduuid
                fmt::fmt
                picojson::picojson
                CLI11::CLI11
                PkgConfig::gstreamer
                PkgConfig::gstreamer-app
                PkgConfig::gstreamer-audio
                PkgConfig::gstreamer-video
        )

    set(mxl_gst_tools mxl-gst-sink mxl-gst-testsrc mxl-gst-looping-filesrc)

    # These tools copy v210 pictures with cudaMemcpy. They are separate from mxl-gst-testsrc and mxl-gst-sink,
    # which use the host pointer API.
    if (MXL_ENABLE_CUDA_PAYLOAD)
        if (NOT TARGET CUDA::cudart_static)
            find_package(CUDAToolkit REQUIRED)
        endif()

        add_executable(mxl-gst-cuda-testsrc cuda_testsrc.cpp)
        add_executable(mxl-gst-cuda-sink cuda_sink.cpp)
        foreach(target mxl-gst-cuda-testsrc mxl-gst-cuda-sink)
            set_target_properties(${target}
                    PROPERTIES
                        POSITION_INDEPENDENT_CODE   ON
                        VISIBILITY_INLINES_HIDDEN   ON
                        C_VISIBILITY_PRESET         hidden
                        CXX_VISIBILITY_PRESET       hidden
                        C_EXTENSIONS                OFF
                        CXX_EXTENSIONS              OFF
                        C_STANDARD                  ${MXL_C_STANDARD}
                        CXX_STANDARD                ${MXL_CXX_STANDARD}
                )
            target_link_libraries(${target}
                    PRIVATE
                        mxl
                        CUDA::cudart_static
                        fmt::fmt
                        picojson::picojson
                        CLI11::CLI11
                        PkgConfig::gstreamer
                        PkgConfig::gstreamer-app
                        PkgConfig::gstreamer-video
                )
            # libmxl loads the backend at run time, so nothing links it.
            add_dependencies(${target} mxl-payload-cuda-linear)
        endforeach()
        list(APPEND mxl_gst_tools mxl-gst-cuda-testsrc mxl-gst-cuda-sink)
    endif()

    # Install targets
    install(TARGETS ${mxl_gst_tools}
            COMPONENT ${PROJECT_NAME}-tools
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
endif()
