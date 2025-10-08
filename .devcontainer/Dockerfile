# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

ARG BASE_IMAGE_VERSION=24.04
FROM ubuntu:${BASE_IMAGE_VERSION}

ARG DEBIAN_FRONTEND=noninteractive

RUN echo 'vm.swappiness=0'          >> /etc/sysctl.conf

COPY ./scripts/ /tmp/scripts

# Install basic and runtime dependencies
RUN apt clean all && apt-get update \
 && apt-get install -y --no-install-recommends \
    wget \
    curl \
    zip \
    unzip \
    tar \
    lsb-release \
    apt-transport-https \
    ca-certificates \
    software-properties-common \
    git \
    git-lfs \
    openssh-client \
    pkg-config \
    build-essential \
    gdb \
    nasm \
    doxygen \
    graphviz \
    autoconf \
    automake \
    libtool \
    pkg-config \
    bison \
    flex \
    htop \
    ccache \
    rustup \
    librdmacm-dev


ARG CLANG_VERSION=19
RUN apt-get install -y --no-install-recommends \
    "clang-${CLANG_VERSION}" \
    "clang-tools-${CLANG_VERSION}" \
    "clang-tidy-${CLANG_VERSION}" \
    "clang-format-${CLANG_VERSION}" \
    "llvm-${CLANG_VERSION}" \
    cmake \
    ninja-build

# Make the selected clang version the default alternative
RUN /tmp/scripts/debian/register-clang-version.sh "${CLANG_VERSION}" 100 

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-x 

RUN /tmp/scripts/common/libfabric/install.sh

# Install GST dependencies for plugins
RUN apt-get install -y --no-install-recommends \
    libglib2.0-dev \
    libgdk-pixbuf2.0-dev \
    libcairo2-dev \
    libpango1.0-dev \
    libgraphene-1.0-dev \
    libgtk-4-dev \
    libgirepository1.0-dev \
    libatk1.0-dev

# Remove the default ubuntu user if it exists
RUN userdel --remove ubuntu >/dev/null 2>&1 || true
RUN groupdel ubuntu >/dev/null 2>&1 || true

# Set default user
ARG USERNAME=devcontainer
ARG USER_UID=1000
ARG USER_GID=${USER_UID}

# Create the user
RUN groupadd --gid "${USER_GID}" "${USERNAME}" \
 && useradd --uid "${USER_UID}" --gid "${USER_GID}" -m -s /bin/bash "${USERNAME}" \
 && apt-get install -y sudo \
 && printf '%s ALL=(root) NOPASSWD:ALL\n' "${USERNAME}" > "/etc/sudoers.d/${USERNAME}" \
 && chmod 0440 "/etc/sudoers.d/${USERNAME}"

USER "${USERNAME}"
WORKDIR "/home/${USERNAME}"

# Install vcpkg
RUN git clone https://github.com/microsoft/vcpkg \
 && ./vcpkg/bootstrap-vcpkg.sh --disableMetrics

ENV VCPKG_ROOT="/home/${USERNAME}/vcpkg"

COPY ./scripts/ /tmp/scripts/
RUN /tmp/scripts/common/rust/install-rust.sh
