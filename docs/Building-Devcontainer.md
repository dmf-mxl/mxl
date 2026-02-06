<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Devcontainer Build Environment

This is the preferred option for development on WSL2 or native Linux desktop. This method is self-contained, provides a predictable build environment (through a dynamically built container) and pre-configured set of VSCode extensions defined in the devcontainer definition file.

## Steps

1. **Install VSCode**
   - Download from https://code.visualstudio.com/

2. **Install the DevContainer extension**
   - Open VSCode
   - Install extension ID: `ms-vscode-remote.remote-containers`

3. **Install Docker**
   - Inside WSL2 or native Linux: `sudo apt install docker.io`
   - Add user to docker group: `sudo usermod -aG docker $USER`
   - Log out and log back in for group changes to take effect

4. **Install Docker Buildx**
   - Ubuntu: `sudo apt install docker-buildx`

5. **Open the MXL source code folder using VSCode**
   - In WSL2: `code <mxl_directory>`
   - **NOTE:** If not running under WSL2, remove the 2 mount points defined in the devcontainer.json you intend to use for development.
   - For example, if you intend to use the Ubuntu 24.04 devcontainer, edit the `.devcontainer/ubuntu24/devcontainer.json` file and remove the content of the "mounts" array. These mount points are only needed for X/WAYLAND support in WSL2. Their presence will prevent the devcontainer from loading correctly when running on a native Linux system.

6. **Reopen in container**
   - VSCode will detect that this folder contains a devcontainer definition. It will prompt you with a dialog "Reopen in dev container". Click this dialog.
   - If the dialog does not appear, invoke the command: `CTRL-SHIFT-P -> Dev Containers: Reopen in container`

7. **Build inside the container**
   - Once the container is running, open a terminal in VSCode
   - The build system is already configured
   - Run `cmake --build build/Linux-GCC-Debug --target all`

[Back to Building overview](./Building.md)
