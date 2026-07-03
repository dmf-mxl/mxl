# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
FROM debian:trixie-slim AS base

RUN apt-get update && apt-get install -y \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-tools \
    gstreamer1.0-x \
    dumb-init \
    && rm -r /var/lib/apt/lists/*

#-----------------------------------------------------------------
FROM base AS builder

ENV HOME="/root" 

# Install dependencie for SDK & gst demos
ARG CLANG_VERSION=19
RUN apt-get update && apt-get install -y \
    "clang-${CLANG_VERSION}" \
    "clang-tools-${CLANG_VERSION}" \
    "clang-tidy-${CLANG_VERSION}" \
    "clang-format-${CLANG_VERSION}" \
    "libclang-rt-${CLANG_VERSION}-dev" \
    build-essential bison flex \
    cmake ninja-build \
    curl zip unzip tar \
    pkg-config \
    git \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev

RUN git clone https://github.com/microsoft/vcpkg.git "${HOME}/vcpkg" \
    && cd "${HOME}/vcpkg" \
    && ./bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT="${HOME}/vcpkg"

ENV MXL_REPO_ROOT="${HOME}/mxl"
RUN mkdir "$MXL_REPO_ROOT"
COPY ./ "$MXL_REPO_ROOT"

# Build MXL SDK
RUN cmake -S "$MXL_REPO_ROOT" --preset Linux-GCC-Release -DCMAKE_INSTALL_PREFIX=/usr
RUN ninja -C "${MXL_REPO_ROOT}/build/Linux-GCC-Release"
RUN ninja -C "${MXL_REPO_ROOT}/build/Linux-GCC-Release" install

# Install libfabric
RUN apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    libtool
RUN ${MXL_REPO_ROOT}/.devcontainer/scripts/common/libfabric/install.sh

# Install GST dependencies for rust plugins
RUN apt-get install -y --no-install-recommends \
    libglib2.0-dev \
    libcairo2-dev \
    libpango1.0-dev \
    libgraphene-1.0-dev \
    libgtk-4-dev \
    libgirepository1.0-dev \
    libatk1.0-dev \
    rustup

RUN ${MXL_REPO_ROOT}/.devcontainer/scripts/common/rust/install-rust.sh
ENV CARGO_HOME="${HOME}/.cargo/"
ENV RUSTUP_HOME="${HOME}/.rustup/"

RUN cd "${MXL_REPO_ROOT}/rust/" \
    && cargo build --release --features mxl-sys/mxl-not-built,mxl/mxl-not-built \
    && install -m 0644 "${MXL_REPO_ROOT}/rust/target/release/libgstmxl.so" \
         "/usr/lib/$(gcc -dumpmachine)/gstreamer-1.0/"

# Build gst-plugin-rsclosedcaption from gst-plugins-rs. The closed-caption
# and ST 2038 elements (cctost2038anc, st2038anctocc, tttocea608, cea608tott)
# come from this plugin and aren't packaged on Ubuntu 24.04.
#
# Tag 0.14.5 is the latest release in the gst-plugins-rs 0.14 series.
# 0.14 is the first release with cctost2038anc / st2038anctocc /
# st2038ancdemux / st2038ancmux (added in 0.14.0) and targets gstreamer-rs
# 0.24, matching gst-mxl-rs and the system GStreamer 1.24 ABI from apt.
# 0.13.x doesn't ship the ST 2038 elements; main / 0.15.x targets
# gstreamer-rs 0.26.
RUN git clone --depth 1 --branch 0.14.5 \
        https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs.git \
        /tmp/gst-plugins-rs \
    && cd /tmp/gst-plugins-rs \
    && cargo build --release -p gst-plugin-closedcaption \
    && install -m 0644 target/release/libgstrsclosedcaption.so \
         "/usr/lib/$(gcc -dumpmachine)/gstreamer-1.0/" \
    && cd - \
    && rm -rf /tmp/gst-plugins-rs

# ------------------------------------------------------------
# Runtime images
FROM base AS mxl

ARG  LIB="/usr/lib/x86_64-linux-gnu"
COPY --from=builder "${LIB}/libmxl*" "${LIB}/"
COPY --from=builder "/usr/bin/mxl-*" "/usr/bin/"
COPY --from=builder "${LIB}/gstreamer-1.0/libgstmxl.so" "${LIB}/gstreamer-1.0/"
COPY --from=builder "${LIB}/gstreamer-1.0/libgstrsclosedcaption.so" "${LIB}/gstreamer-1.0/"

RUN useradd -m mxl
USER mxl
WORKDIR /home/mxl

FROM mxl AS gst-mxl-rs
ENTRYPOINT ["gst-launch-1.0"]

FROM mxl AS mxl-info
ENTRYPOINT ["/usr/bin/mxl-info"]

FROM mxl AS mxl-gst-testsrc
ENTRYPOINT ["/usr/bin/mxl-gst-testsrc"]

FROM mxl AS mxl-fake-reader
COPY examples/scripts/fake-reader.sh /home/mxl/fake-reader.sh
ENTRYPOINT ["/usr/bin/dumb-init", "--", "/home/mxl/fake-reader.sh"]
