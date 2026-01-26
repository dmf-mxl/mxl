#!/bin/sh

# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

set -e

usage() {
    echo "usage: $0 <build_dir> <vcpkg_install_dir> <vcpkg_triplet> <BUILD_SHARED_LIBS: ON|OFF> <MXL_ENABLE_FABRICS_OFI: ON|OFF> <MXL_LINKER_FLAG=<linker-selection-flag>> <c_compiler>" >&2
    echo "  build_dir         Path to the CMake build directory." >&2
    echo "  vcpkg_install_dir vcpkg install directory" >&2
    echo "  vcpkg_triplet     vcpkg target triplet used for the build." >&2
    echo "  BUILD_SHARED_LIBS Indicates whether the build uses shared libraries (ON or OFF)." >&2
    echo "  MXL_ENABLE_FABRICS_OFI Enable libmxl-fabrics.pc test (ON or OFF)." >&2
    echo "  MXL_LINKER_FLAG   Optional compiler linker-selection flag (e.g. -fuse-ld=lld); empty means use default linker." >&2
    echo "  c_compiler        Path to the C compiler executable." >&2
}

if [ "$#" -ne 7 ]; then
    usage
    exit 1
fi

BUILD_DIR="$1"
VCPKG_INSTALL_DIR="$2"
VCPKG_TRIPLET="$3"
BUILD_SHARED_LIBS="$4"
MXL_ENABLE_FABRICS_OFI="$5"
MXL_LINKER_FLAG="${6#MXL_LINKER_FLAG=}"
C_COMPILER="$7"

case "$BUILD_SHARED_LIBS" in
  ON|OFF) ;;
  *) usage; exit 2 ;;
esac

case "$MXL_ENABLE_FABRICS_OFI" in
  ON|OFF) ;;
  *) usage; exit 2 ;;
esac

export PKG_CONFIG_PATH="$BUILD_DIR/pkgconfig_test:$VCPKG_INSTALL_DIR/$VCPKG_TRIPLET/lib/pkgconfig:${PKG_CONFIG_PATH}"

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
           $(pkg-config --cflags --libs --static libmxl) \
           -L"$BUILD_DIR/lib/internal" \
           -o mxl_link_test
    else
        "$C_COMPILER" mxl_link_test.c \
            $MXL_LINKER_FLAG \
            $(pkg-config --cflags --libs libmxl) \
            "-Wl,-rpath,$BUILD_DIR/lib" \
            -o mxl_link_test
    fi

    mkdir ./mxl_link_test_domain
    ./mxl_link_test

    rm -rf ./mxl_link_test_domain
    rm  ./mxl_link_test
    rm  ./mxl_link_test.c
)

if [ "$MXL_ENABLE_FABRICS_OFI" = ON ]; then
(
    set -e
    cd $TEST_DIR

    cat > fabrics_link_test.c <<'EOF'
#include <mxl/fabrics.h>
int main(void)
{
    (void)&mxlFabricsCreateInstance;
    (void)&mxlFabricsDestroyInstance;
    return 0;
}
EOF

    # libfabric may be supplied at a non-default path via PKG_CONFIG_PATH
    FABRIC_LIBDIR=$(pkg-config --variable=libdir libfabric)

    if [ "$BUILD_SHARED_LIBS" = "OFF" ]; then
        "$C_COMPILER" fabrics_link_test.c \
           $MXL_LINKER_FLAG \
           $(pkg-config --cflags --libs --static libmxl-fabrics) \
           -L"$BUILD_DIR/lib/internal" \
           "-Wl,-rpath,$FABRIC_LIBDIR" \
           -o fabrics_link_test
    else
         "$C_COMPILER" fabrics_link_test.c \
            $MXL_LINKER_FLAG \
            $(pkg-config --cflags --libs libmxl-fabrics libfabric) \
            "-Wl,-rpath,$BUILD_DIR/lib" \
            "-Wl,-rpath,$BUILD_DIR/lib/fabrics/ofi" \
            "-Wl,-rpath,$FABRIC_LIBDIR" \
            -o fabrics_link_test
    fi

    ./fabrics_link_test

    rm ./fabrics_link_test
    rm ./fabrics_link_test.c
)
fi

rmdir $TEST_DIR
