<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MXL : Media eXchange Layer

[![Build Pipeline](https://github.com/dmf-mxl/mxl/actions/workflows/build.yml/badge.svg)](https://github.com/dmf-mxl/mxl/actions/workflows/build.yml)
[![GitHub release](https://img.shields.io/github/v/release/dmf-mxl/mxl)](https://github.com/dmf-mxl/mxl/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE.txt)
[![Issues](https://img.shields.io/github/issues/dmf-mxl/mxl)](https://github.com/dmf-mxl/mxl/issues)

[Go straight to documentation](#governance)

# What is MXL?

- MXL is an open-source SDK designed for high-performance and container-friendly shared media exchange across on‑premise or cloud environments.
- It provides an open and non-proprietary exchange layer, helping to promote interoperability by avoiding the need for conversion between vendors. Such a layer is required by the [EBU Dynamic Media Facility Reference Architecture](https://tech.ebu.ch/dmf/ra).
- This repository contains an open-source SDK that specifies MXL through implementation, including how video, audio and data payloads are represented in shared memory, and the timing of those payloads.

## Key Characteristics

MXL: 

- is implemented in C++, exposed via a C API with Rust bindings available
- supports for V210 video, Float32 audio and ANC data
- implements zero-copy media sharing or low-copy media movement
- is asynchronous by design
- is a thin library with minimal external dependencies
- can be deployed on local (on-premise) or cloud compute
- is suitable for use in containerised environments

## APIs

### Flow API (MXL v1.0 on)

Enables shared-memory access for zero-copy media workflows.

### Fabric API (tbc)

Enables memory sharing through controlled data movement.

# Getting Started

- To see MXL moving test video and audio between containers, without cloning the repo or installing a compiler on the host, run the command below on Linux (x86_64). It downloads sources, ensures Docker is installed, builds the example images from source, and starts the [Docker Compose example](examples/README.md). The first build compiles the SDK inside Docker and takes several minutes. If Docker is missing, the script uses `sudo` to install it from distro packages (`docker.io` on Ubuntu/Debian) where those exist (your password prompt is expected). This is not a tutorial, and it does not install MXL libraries on the host.

  ```bash
  curl -fsSL https://raw.githubusercontent.com/dmf-mxl/mxl/main/examples/bootstrap.sh | bash -s -- --yes --up
  ```

  From a checkout, run `./examples/bootstrap.sh --up` instead. When the stack is up, `docker logs mxl-example-video-flow-info-1` should show updating flow info. Stop with Ctrl+C, then `docker compose down` from the `examples` directory.
- A guided workshop with exercises is in the [cbcrc/mxl-hands-on](https://github.com/cbcrc/mxl-hands-on) repo.
- To build the SDK on the host (devcontainer or CMake), see [here](docs/Building.md).
- Example tools and GStreamer pipelines are [here](docs/Tools.md).
- Docker Compose details and a Kubernetes example are [here](examples/README.md).

# Motivation

Rapid advances in computing power and network infrastructure are transforming the landscape of live media production. The professional broadcast industry is gradually moving away from hardware-centric systems and towards software-defined solutions, promising far greater flexibility, scalability, and operational agility. However, this shift into a more virtualised, “dematerialised” environment also introduces substantial interoperability challenges. Multiple vendors, proprietary frameworks, and disparate technology stacks can prevent diverse systems from integrating seamlessly across distributed networks, potentially inhibiting innovation and restricting the efficiency of modern broadcast workflows.

To address these challenges, the [EBU Dynamic Media Facility (DMF)](https://tech.ebu.ch/groups/dmf) initiative proposes a standardised architecture inspired by the cloud-hyperscaler model. In this architecture, discrete “media functions”, the modular building blocks responsible for ingesting, processing, and delivering content, are deployed onto a common container-based platform. These functions can be provisioned and scaled on-demand, and strategically placed wherever compute, storage, and bandwidth are most readily available, whether on-premises, at the network edge, or in public or private clouds.

At the heart of the [DMF architecture](https://tech.ebu.ch/publications/white-paper-2024-09-03) lies the Media exchange layer, a high-performance data plane designed to simplify and accelerate communication between these distributed media functions. This enables entirely new production paradigms, including asynchronous “faster-than-live” workflows, allowing teams to produce content more flexibly and quickly than traditional linear models permit. Moreover, its extensible design supports evolving exchange mechanisms and new media formats as they emerge, ensuring that the architecture is well equipped to evolve alongside technological progress. MXL is an open-source implementation of that layer.

![docs/Media eXchange Layer.png](https://github.com/dmf-mxl/mxl/blob/53e889c888b2daceb4bf550943f3a194f559f182/docs/Media%20eXchange%20Layer.png "MXL Layer Diagram")

In order to encourage broad industry adoption, the European Broadcasting Union (EBU) and the North American Broadcasters Association (NABA) are pursuing an “implement-first” strategy. This practical, hands-on approach involves close collaboration with broadcasters and technology suppliers to produce an open-source software development kit that promotes interoperability and showcases real-world use cases for the MXL. The first alpha version of this SDK was released in June 2025. Ultimately, the DMF initiative aspires to establish a new baseline for open, interoperable software-based live production, a foundation that is robust, future-proof, and capable of sustaining innovation across the entire media ecosystem.


# Learning More

## Governance

- The project governance principles: [GOVERNANCE.md](GOVERNANCE/GOVERNANCE.md) and [Technical Charter](GOVERNANCE/CHARTER.pdf)
- How to contribute: [CONTRIBUTING.md](CONTRIBUTING.md)
- How to report a vulnerability: [SECURITY.md](SECURITY.md)
- [Code of conduct](CODE_OF_CONDUCT.md)

## Technical Documentation

- [MXL Architecture](docs/Architecture.md)
- [SDK Usage](docs/Usage.md)
- [Building options](docs/Building.md)
- [Tools](docs/Tools.md)

## License

This code is covered by the [Apache v2 license](./LICENSE.txt)
