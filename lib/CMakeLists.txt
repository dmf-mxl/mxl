# Files to be compiled into the library
add_library(mxl SHARED
    include/mxl/flow.h
    include/mxl/mxl.h
    include/mxl/time.h
    src/flow.cpp
    src/mxl.cpp
    src/time.cpp
    src/internal/DomainWatcher.hpp
    src/internal/DomainWatcher.cpp
    src/internal/Flow.hpp
    src/internal/Flow.cpp
    src/internal/FlowParser.hpp
    src/internal/FlowParser.cpp
    src/internal/FlowManager.hpp
    src/internal/FlowManager.cpp
    src/internal/Instance.hpp
    src/internal/Instance.cpp
    src/internal/Logging.hpp
    src/internal/Logging.cpp
    src/internal/PosixFlowReader.hpp
    src/internal/PosixFlowReader.cpp
    src/internal/PosixFlowWriter.hpp
    src/internal/PosixFlowWriter.cpp
    src/internal/SharedMem.hpp
    src/internal/SharedMem.cpp
    src/internal/Sync.hpp
    src/internal/Sync.cpp
    src/internal/Time.hpp
    src/internal/Time.cpp
)

find_package(stduuid CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(picojson REQUIRED)

target_link_libraries(mxl PRIVATE stduuid spdlog::spdlog fmt::fmt picojson::picojson pthread)
target_include_directories(mxl PUBLIC
    $<BUILD_INTERFACE:${mxl_SOURCE_DIR}/lib/include>
    $<BUILD_INTERFACE:${mxl_BINARY_DIR}>
)

set_target_properties(mxl PROPERTIES
    SOVERSION ${mxl_VERSION_MAJOR}
    VERSION ${mxl_VERSION}
)

add_subdirectory(tests)

# Install targets
install(TARGETS mxl
    EXPORT mxlTarget
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
    PATTERN *.h.in EXCLUDE
)

# Generate CMake config files
include(CMakePackageConfigHelpers)
configure_package_config_file(${mxl_SOURCE_DIR}/cmake/mxlConfig.cmake.in
    mxlConfig.cmake
    INSTALL_DESTINATION lib/cmake/mxl
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mxlConfigVersion.cmake
    VERSION ${mxl_VERSION}
    COMPATIBILITY SameMinorVersion
)
# Install CMake config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mxlConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mxlConfigVersion.cmake
    DESTINATION lib/cmake/mxl
)
install(EXPORT mxlTarget
    NAMESPACE mxl::
    DESTINATION lib/cmake/mxl
    FILE mxlTarget.cmake
)