<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Troubleshooting Common Build Issues

## Issue: vcpkg not found

**Symptom:**
```
CMake Error: Could not find vcpkg
```

**Solution:**
```bash
# Set VCPKG_ROOT environment variable
export VCPKG_ROOT=~/vcpkg

# Or specify vcpkg toolchain file explicitly
cmake -B build --preset Linux-GCC-Debug \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

## Issue: GStreamer development files not found

**Symptom:**
```
Could not find GStreamer
```

**Solution:**
```bash
# Ubuntu/Debian
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

# macOS
# Download from https://gstreamer.freedesktop.org/documentation/installing/on-mac-osx.html
```

## Issue: Out of memory during build

**Symptom:**
```
c++: fatal error: Killed signal terminated program cc1plus
```

**Solution:**
```bash
# Limit parallel jobs
cmake --build build/Linux-GCC-Debug --target all -j2

# Or use only one job
cmake --build build/Linux-GCC-Debug --target all -j1
```

## Issue: Linker errors with static builds

**Symptom:**
```
undefined reference to `pthread_create'
```

**Solution:**
```bash
# Ensure PIC is enabled for static builds
cmake -B build --preset Linux-GCC-Debug \
    -DBUILD_SHARED_LIBS=OFF \
    -DMXL_ENABLE_PIC=ON
```

## Issue: Tests fail with "Permission denied"

**Symptom:**
```
Test failed: Permission denied accessing /dev/shm/mxl
```

**Solution:**
```bash
# Ensure /dev/shm is writable
sudo chmod 1777 /dev/shm

# Or create a user-specific MXL domain
mkdir -p ~/mxl-test-domain
export MXL_TEST_DOMAIN=~/mxl-test-domain
```

## Issue: Sanitizer build fails at runtime

**Symptom:**
```
AddressSanitizer: CHECK failed
```

**Solution:**
```bash
# Increase memory limits for sanitizers
ulimit -v unlimited

# Run with suppression file if needed
export ASAN_OPTIONS=suppressions=asan.supp

# Rebuild with sanitizer
cmake --build build/Linux-GCC-AddressSanitizer --target all
```

[Back to Building overview](./Building.md)
