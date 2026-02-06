<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Using with CMake

The MXL provides a CMake package configuration file that allows for easy integration into your project. If it is installed in a non-default location, you may need to specify its root path using `CMAKE_PREFIX_PATH`:

```bash
cmake -DCMAKE_PREFIX_PATH=/home/username/mxl-sdk ..
```

## Basic usage

Below is a minimal example of how to use the MXL in your project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(mxl-test LANGUAGES CXX)

find_package(mxl CONFIG REQUIRED)

add_executable(mxl-test main.cpp)
target_link_libraries(mxl-test PRIVATE mxl::mxl)
```

## Advanced CMake integration

```cmake
cmake_minimum_required(VERSION 3.20)
project(my-mxl-app LANGUAGES CXX)

# Find MXL
find_package(mxl CONFIG REQUIRED)

# Create executable
add_executable(my-app
    src/main.cpp
    src/video_writer.cpp
    src/audio_reader.cpp
)

# Link MXL
target_link_libraries(my-app PRIVATE mxl::mxl)

# Set C++ standard
target_compile_features(my-app PRIVATE cxx_std_17)

# Optional: Link against Rust bindings if built
if(TARGET mxl::mxl-rs)
    target_link_libraries(my-app PRIVATE mxl::mxl-rs)
endif()
```

[Back to Building overview](./Building.md)
