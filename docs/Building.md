## Building

### Option 1 : Devcontainer build environment

This is the preferred option for development on WSL2 or native linux desktop. This method is self contained, provides a predictable build environment (through a dynamically built container) and pre-configured set of VSCode extensions defined in the devcontainer definition file.

1. Install [VSCode](https://code.visualstudio.com/)
2. Install the [DevContainer extension](vscode:extension/ms-vscode-remote.remote-containers)
3. Install docker (inside wsl2 or native linux). Make sure the current user is part of the docker group.
   - On Ubuntu, this would be: `sudo apt install docker.io`
4. Install docker buildx.
   - On Ubuntu, this would be: `sudo apt install docker-buildx`
5. Install the [NVIDIA Container Runtime](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html)
6. Open the MXL source code folder using VSCode. In wsl2 this can be done by launching `code <mxl_directory>`
   - NOTE: If not running under WSL2, remove the 2 mount points defined in the devcontainer.json you intend to use for development.  For example, if you intend to use the Ubuntu 24.04 devcontainer, edit the .devcontainer/ubuntu24/devcontainer.json file and remove the content of the "mounts" array.  These mount points are only needed for X/WAYLAND support in WSL2.  Their presence will prevent the devcontainer to load correctly when running on a native Linux system.    
7. VSCode will detect that this folder contains a devcontainer definition. It will prompt you with a dialog "Reopen in dev container". Click this dialog. If the dialog does not appear, you can invoke the command: `CTRL-SHIFT-P -> Dev Containers : Reopen in container`

### Option 2: CMake with presets

_Note: the following instructions apply to Ubuntu 22.04._

1. Install all apt packages specified in the [Dockerfile](.devcontainer/Dockerfile)
2. Install vcpkg as done in the [Dockerfile](.devcontainer/Dockerfile)
3. Create a build directory and invoke cmake with a build preset. the list of available presets is:

```bash
  "Linux-GCC-Debug"
  "Linux-GCC-Release"
  "Linux-GCC-AddressSanitizer"
  "Linux-GCC-ThreadSanitizer"
  "Linux-GCC-UBSanitizer"
  "Linux-Clang-Debug"
  "Linux-Clang-Release"
  "Linux-Clang-AddressSanitizer"
  "Linux-Clang-UBSanitizer"
  "Darwin-Clang-Debug"
  "Darwin-Clang-Release"
  "Darwin-Clang-AddressSanitizer"
  "Darwin-Clang-UBSanitizer"
```

For example:

```bash
# Generate the build files using the Linux-Clang-Debug preset
mkdir build
cd build
cmake .. --preset Linux-Clang-Debug

# Build everything
cmake --build build/Linux-Clang-Debug --target all
```

### macOS notes

1. Install the [Homebrew](https://brew.sh) package manager
2. Install doxygen and ccache through brew: `brew install doxygen ccache`
3. Install the GStreamer runtime and developement packages according to [these instructions](https://gstreamer.freedesktop.org/documentation/installing/on-mac-osx.html?gi-language=c#download-and-install-the-sdk)

### macOS notes

1. Install the [Homebrew](https://brew.sh) package manager
2. Install doxygen and ccache through brew: `brew install doxygen ccache`
3. Install the GStreamer runtime and development packages according to [these instructions](https://gstreamer.freedesktop.org/documentation/installing/on-mac-osx.html?gi-language=c#download-and-install-the-sdk)

## Using with CMake

The MXL provides a CMake package configuration file that allows for easy integration into your project. If it is installed in a non-default location, you may need to specify its root path using `CMAKE_PREFIX_PATH`:

```bash
cmake -DCMAKE_PREFIX_PATH=/home/username/mxl-sdk ..
```

### Basic usage

Below is a minimal example of how to use the MXL in your project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(mxl-test LANGUAGES CXX)
find_package(mxl CONFIG REQUIRED)
add_executable(mxl-test main.cpp)
target_link_libraries(mxl-test PRIVATE mxl::mxl)
```
