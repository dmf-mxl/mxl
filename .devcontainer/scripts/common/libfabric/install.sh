#!/bin/bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

set -e
set -x

git clone -b v2.2.0 https://github.com/ofiwg/libfabric.git

pushd libfabric >/dev/null

./autogen.sh
./configure \
    --prefix=/usr \
    --disable-kdreg2 \
    --disable-memhooks-monitor \
    --disable-uffd-monitor

make install "-j$(nproc)"

popd >/dev/null

exit 0
