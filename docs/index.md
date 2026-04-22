<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

#  MXL 

## Documentation version: v0.1.1

<a href="https://github.com/dmf-mxl/mxl">
  <img src="https://github.githubassets.com/images/modules/logos_page/GitHub-Mark.png" width="48" alt="GitHub Logo"/>
</a> [Contribute!](https://github.com/dmf-mxl/mxl)

## About the Media eXchange Layer SDK

- MXL is an open-source SDK designed for high-performance and container-friendly shared media exchange across on‑premise or cloud environments.
- It provides an open and non-proprietary exchange layer, helping to promote interoperability by avoiding the need for conversion between vendors. Such a layer is required by the [EBU Dynamic Media Facility Reference Architecture](https://tech.ebu.ch/dmf/ra).
- This repository contains an open-source SDK that specifies MXL through implementation, including how video, audio and data payloads are represented in shared memory, and the timing of those payloads.

## Key characteristics

MXL:

- is implemented in C++, exposed via a C API with Rust bindings available
- supports for V210 video, Float32 audio and ANC data
- implements zero-copy media sharing or low-copy media movement
- is asynchronous by design
- is a thin library with minimal external dependencies
- can be deployed on local (on-premise) or cloud compute
- is suitable for use in containerised environments
- code is covered by the Apache v2 license

!!! info
    For more information use the links in the sidebar.
