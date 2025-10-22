#!/bin/bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

git clone -b v2.2.0 https://github.com/ofiwg/libfabric.git || exit 1

pushd libfabric >/dev/null || exit 1

./autogen.sh || exit 1
./configure \
    --prefix=/usr \
    --disable-kdreg2 \
    --disable-memhooks-monitor \
    --disable-uffd-monitor || exit 1

make install -j || exit 1

popd >/dev/null || exit 1

exit 0
