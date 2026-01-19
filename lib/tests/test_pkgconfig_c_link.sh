#!/bin/sh

# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

set -e

usage() {
    echo "usage: $0 <build_dir> <vcpkg_install_dir> <vcpkg_triplet> <BUILD_SHARED_LIBS: ON|OFF> <MXL_LINKER_FLAG=<linker-selection-flag>> <c_compiler>" >&2
    echo "  build_dir         Path to the CMake build directory." >&2
    echo "  vcpkg_install_dir vcpkg install directory" >&2
    echo "  vcpkg_triplet     vcpkg target triplet used for the build." >&2
    echo "  BUILD_SHARED_LIBS Indicates whether the build uses shared libraries (ON or OFF)." >&2
    echo "  MXL_LINKER_FLAG   Optional compiler linker-selection flag (e.g. -fuse-ld=lld); empty means use default linker." >&2
    echo "  c_compiler        Path to the C compiler executable." >&2
}

if [ "$#" -ne 6 ]; then
    usage
    exit 1
fi

BUILD_DIR="$1"
VCPKG_INSTALL_DIR="$2"
VCPKG_TRIPLET="$3"
BUILD_SHARED_LIBS="$4"
MXL_LINKER_FLAG="${5#MXL_LINKER_FLAG=}"
C_COMPILER="$6"

PKGCFG_STATIC=
RPATH_FLAGS=
if [ "$BUILD_SHARED_LIBS" = "OFF" ]; then
    PKGCFG_STATIC="--static"
elif [ "$BUILD_SHARED_LIBS" = "ON" ]; then
    RPATH_FLAGS="-Wl,-rpath,$BUILD_DIR/lib"
else
    usage
    exit 2
fi

export PKG_CONFIG_PATH="$BUILD_DIR/pkgconfig_test:$VCPKG_INSTALL_DIR/$VCPKG_TRIPLET/lib/pkgconfig"

TEST_DIR="$(mktemp -d /tmp/mxl_pkgconfig_test.XXXXXX)"

(
    set -e
    cd $TEST_DIR

    cat > mxl_link_test.c <<'EOF'
#include <mxl/mxl.h>
#include <stddef.h>
#include <stdlib.h>
int main(void)
{
    mxlInstance instance = mxlCreateInstance("./mxl_link_test_domain", NULL);
    int rc = !!instance ? EXIT_SUCCESS : EXIT_FAILURE;
    mxlDestroyInstance(instance);
    return rc;
}
EOF

    if [ "$BUILD_SHARED_LIBS" = "OFF" ]; then
        "$C_COMPILER" mxl_link_test.c \
           $MXL_LINKER_FLAG \
           $(pkg-config --cflags --libs $PKGCFG_STATIC libmxl) \
           -L"$BUILD_DIR/lib/internal" \
           -o mxl_link_test
    else
        "$C_COMPILER" mxl_link_test.c \
            $MXL_LINKER_FLAG \
            $(pkg-config --cflags --libs $PKGCFG_STATIC libmxl) \
            $RPATH_FLAGS \
            -o mxl_link_test
    fi

    mkdir mxl_link_test_domain
    ./mxl_link_test
)

rm -rf $TEST_DIR/mxl_link_test_domain
rm  $TEST_DIR/mxl_link_test
rm  $TEST_DIR/mxl_link_test.c
rmdir $TEST_DIR

