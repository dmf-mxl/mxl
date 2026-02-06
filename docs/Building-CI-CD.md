<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: CI/CD Integration

## GitHub Actions example

```yaml
name: Build and Test

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential cmake git pkg-config ccache doxygen

      - name: Install vcpkg
        run: |
          git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
          ~/vcpkg/bootstrap-vcpkg.sh
          echo "VCPKG_ROOT=~/vcpkg" >> $GITHUB_ENV

      - name: Configure
        run: cmake -B build --preset Linux-GCC-Release

      - name: Build
        run: cmake --build build/Linux-GCC-Release --target all

      - name: Test
        run: |
          cd build/Linux-GCC-Release
          ctest --output-on-failure
```

## GitLab CI example

```yaml
build:
  image: ubuntu:24.04
  before_script:
    - apt-get update && apt-get install -y build-essential cmake git pkg-config ccache doxygen
    - git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg
    - /opt/vcpkg/bootstrap-vcpkg.sh
    - export VCPKG_ROOT=/opt/vcpkg
  script:
    - cmake -B build --preset Linux-GCC-Release
    - cmake --build build/Linux-GCC-Release --target all
    - cd build/Linux-GCC-Release && ctest --output-on-failure
```

[Back to Building overview](./Building.md)
