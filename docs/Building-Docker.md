<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Docker/Container Build Instructions

You can build MXL inside a Docker container without using devcontainers.

## Create a Dockerfile

```dockerfile
FROM ubuntu:24.04

# Install prerequisites
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    ccache \
    doxygen \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg && \
    /opt/vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/opt/vcpkg

# Set working directory
WORKDIR /workspace

# Build command (override in docker run)
CMD ["bash"]
```

## Build in Docker

```bash
# Build container image
docker build -t mxl-builder .

# Build MXL inside container
docker run --rm -v $(pwd):/workspace mxl-builder \
    bash -c "cmake -B build --preset Linux-GCC-Release && \
             cmake --build build/Linux-GCC-Release --target all"
```

[Back to Building overview](./Building.md)
