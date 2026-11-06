<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Bare Metal Linux Compilation Issues

There are two options for compiling the SDK : DevContainer and CMake with presets.  The DevContainer approach is targeting WSL2 environments but can also be used with minor modifications for Linux Desktop environments using Visual Studio Code by commenting out or removing the content of the mounts section of the selected devcontainer.json file.  For example, if using the Ubuntu 24.04 devcontainer environment, comment out or remove [these lines](https://github.com/dmf-mxl/mxl/blob/82b0106638a8caabb2606e2022fb9d0c010287b0/.devcontainer/ubuntu24/devcontainer.json#L21C1-L23C73).