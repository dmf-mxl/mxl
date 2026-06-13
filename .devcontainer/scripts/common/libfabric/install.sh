#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

set -e
set -x

git clone -b v2.5.1 https://github.com/ofiwg/libfabric.git

cd libfabric
./autogen.sh
./configure \
  --prefix=/usr \
  --disable-kdreg2 \
  --disable-memhooks-monitor \
  --disable-uffd-monitor

make install "-j$(nproc)"
