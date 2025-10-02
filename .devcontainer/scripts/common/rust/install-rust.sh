#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

set -eu

RUST_VERSION=1.90.0

if command -v rustup > /dev/null 2>&1; then
    rustup default $RUST_VERSION
else
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain=$RUST_VERSION
    . "$HOME/.cargo/env"
fi

# Install cargo binstall
curl -L --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/cargo-bins/cargo-binstall/main/install-from-binstall-release.sh | bash

cargo binstall cargo-audit --locked
cargo binstall cargo-outdated --locked

# udeps requires the nightly compiler, so using machete (at least for now)
# cargo binstall cargo-udeps --locked
cargo binstall cargo-machete --locked

cargo binstall cargo-deny --locked
cargo binstall cargo-audit --locked
cargo binstall cargo-nextest --locked
