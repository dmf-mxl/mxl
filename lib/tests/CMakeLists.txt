# Add the test executable
add_executable(mxl-tests)
target_compile_features(mxl-tests
        PRIVATE
            cxx_std_20
    )
set_target_properties(mxl-tests
        PROPERTIES
            POSITION_INDEPENDENT_CODE    ON
            VISIBILITY_INLINES_HIDDEN    ON
            C_VISIBILITY_PRESET          hidden
            CXX_VISIBILITY_PRESET        hidden
            C_EXTENSIONS                 OFF
            CXX_EXTENSIONS               OFF
    )

target_sources(mxl-tests
        PRIVATE
        test_domainwatcher.cpp
        test_flowmanager.cpp
        test_flows.cpp
        test_flows_timing.cpp
        test_options.cpp
        test_time.cpp
        test_sharedmem.cpp
        Utils.cpp
        ../src/internal/DomainWatcher.cpp
        ../src/internal/Flow.cpp
        ../src/internal/FlowData.cpp
        ../src/internal/FlowIoFactory.cpp
        ../src/internal/FlowManager.cpp
        ../src/internal/FlowParser.cpp
        ../src/internal/FlowReader.cpp
        ../src/internal/FlowWriter.cpp
        ../src/internal/Instance.cpp
        ../src/internal/PathUtils.cpp
        ../src/internal/PosixContinuousFlowReader.cpp
        ../src/internal/PosixContinuousFlowWriter.cpp
        ../src/internal/PosixDiscreteFlowReader.cpp
        ../src/internal/PosixDiscreteFlowWriter.cpp
        ../src/internal/PosixFlowIoFactory.cpp
        ../src/internal/SharedMemory.cpp
        ../src/internal/Thread.cpp
        ../src/internal/Time.cpp
        ../src/internal/Timing.cpp
        ../src/internal/Sync.cpp
    )

if (NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 REQUIRED)
endif()

if (NOT TARGET stduuid)
    find_package(stduuid CONFIG REQUIRED)
endif ()

if (NOT TARGET spdlog::spdlog)
    find_package(spdlog CONFIG REQUIRED)
endif ()

if (NOT TARGET fmt::fmt)
    find_package(fmt CONFIG REQUIRED)
endif()

if (NOT TARGET picojson::picojson)
    find_package(picojson REQUIRED)
endif()

if (NOT TARGET PcapPlusPlus::Pcap++)
    find_package(PcapPlusPlus CONFIG)
endif()


target_link_libraries(mxl-tests
        PRIVATE
            mxl
            stduuid
            spdlog::spdlog
            fmt::fmt
            picojson::picojson
            Catch2::Catch2WithMain
    )

if (TARGET PcapPlusPlus::Pcap++)
    target_link_libraries(mxl-tests
            PRIVATE
                PcapPlusPlus::Pcap++
        )
endif()

# Enable testing using CTest
include(CTest)
include(Catch)
catch_discover_tests(mxl-tests)

# Copy test files to the build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
