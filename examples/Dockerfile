# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
FROM debian:trixie-slim AS base

RUN apt-get update && apt-get install -y \
   gstreamer1.0-plugins-good \
   gstreamer1.0-plugins-base \
   curl zip unzip tar \
   && rm -r /var/lib/apt/lists/*

RUN curl -LO "https://github.com/Yelp/dumb-init/releases/download/v1.2.5/dumb-init_1.2.5_amd64.deb" \
    && dpkg -i "dumb-init_1.2.5_amd64.deb" \
    && rm -v dumb-init_1.2.5_amd64.deb

RUN useradd -m mxl

FROM base AS builder

ARG CLANG_VERSION=19
RUN apt-get update && apt-get install -y \
    build-essential bison flex \
    cmake ninja-build \
    pkg-config \
    git \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev

USER mxl

WORKDIR /home/mxl

RUN git clone https://github.com/microsoft/vcpkg.git "${HOME}/vcpkg" \
    && cd "${HOME}/vcpkg" \
    && ./bootstrap-vcpkg.sh -disableMetrics

RUN mkdir mxl
COPY CMakeLists.txt mxl/CMakeLists.txt
COPY CMakePresets.json mxl/CMakePresets.json
COPY Doxyfile.in mxl/Doxyfile.in
COPY vcpkg.json mxl/vcpkg.json
COPY lib mxl/lib
COPY cmake mxl/cmake
COPY tools mxl/tools
COPY utils mxl/utils

RUN cmake -S "${HOME}/mxl" --preset Linux-GCC-Release -DCMAKE_INSTALL_PREFIX=/usr
RUN ninja -C "${HOME}/mxl/build/Linux-GCC-Release"

USER 0:0
RUN ninja -C "/home/mxl/mxl/build/Linux-GCC-Release" install

FROM base AS mxl
COPY --from=builder /usr/lib/x86_64-linux-gnu/libmxl* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/bin/mxl-* /usr/bin/
USER mxl
WORKDIR /home/mxl
ENTRYPOINT ["/bin/bash", "-c"]

FROM mxl AS mxl-info
ENTRYPOINT ["/usr/bin/mxl-info"]

FROM mxl AS mxl-gst-testsrc
ENTRYPOINT ["/usr/bin/mxl-gst-testsrc"]

FROM mxl AS mxl-fake-reader
COPY examples/scripts/fake-reader.sh /home/mxl/fake-reader.sh
ENTRYPOINT ["/usr/bin/dumb-init", "--", "/home/mxl/fake-reader.sh"]
