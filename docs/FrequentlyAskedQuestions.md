<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Frequently Asked Questions

## Why is MXL needed?

The Media eXchange Layer (MXL) is a compute-first SDK that shares live uncompressed production video, audio, and metadata between multi-vendor software media functions running on commonly available IT hardware. Existing live media exchange protocols either require timing synchronisation accuracy beyond capabilities of most IT hardware, are often compressed, synchronous, high latency, packetised or proprietary.

Users are expecting to move their real-time media operations onto a software-first and cloud-fit architecture (see question on DMF below), and there isn't currently a suitable interoperable way of exchanging media for such environments.

## What is meant by "compute-first"? How is this different?

MXL makes use of, and prioritises, datacenter and IT technologies over traditional hardware concepts. Shared memory and fabrics technology are used instead of trying to replicate SDI delivery by packetising and sending/receiving RTP packets aligned to the nanosecond. Basically this is the first time we are freeing ourselves from the constraints of interoperation with SDI gateways, etc.

## Who is MXL aimed at?

Developers of software media functions can use MXL so that their code can interoperate with other vendors. Users can require MXL support to help with interoperability between multiple vendors.

## What is the relationship between MXL and DMF?

The DMF (Dynamic Media Facility) is an initiative of the EBU (European Broadcasting Union) to promote an interoperable software-based architecture for high-performance live media operations. DMF defines a layered reference architecture for infrastructure and media functions, where MXL adresses the requirement of its media exchange layer. See https://tech.ebu.ch/groups/dmf for more information. 

## How does MXL work?

MXL uses shared memory to exchange live media. On the same host, a filesystem in RAM is used. Between hosts, MXL will use remote memory access using fabric technologies and protocols such as RoCEv2.

## Who is contributing to MXL?

Requirements have been provided by several media users, including those who contributed to the EBU DMF project, NABA members, and others who have contributed to industry workshops. Several well-known organisations in the broadcast industry have contributed code to the MXL SDK, and you can see details from the GitHub commit history.

## How is the MXL project managed?

The MXL project has been set up via the Linux Foundation: a nonprofit organization that plays a central role in the development and promotion of open-source technologies, including the Linux operating system and many other critical software projects.

A Technical Steering Committee (TSC) provides overall high-level project guidance, and a Requirements Council (RC) complements the TSC by representing user requirements. Contributors submit code to the project, and Maintainers approve code to be included into the project. 

Both the TSC and RC “show their working in public” via Kanban boards on GitHub:  https://github.com/orgs/dmf-mxl/projects. This includes notes from regular meetings.

Details of the project’s charter and governance are provided in this folder: https://github.com/dmf-mxl/mxl/tree/main/GOVERNANCE

## Is MXL a published standard?

MXL is not a standard, it is a publicly available open source code project. This strategy was agreed with users and vendors as a rapid and agile way of meeting requirements and to provide a common machine-readable code base. The open source SDK acts as a reference implementation that provides a straightforward way of achieving interoperability when compared with a human-readable text specification or standard. 

## When did MXL kick-off?

Workshops were held in late 2024 and early 2025 to test industry appetite for the project, agree on first requirements and establish a way forward. The open source project with an early release of the code was announced in [June 2025](https://tech.ebu.ch/news/2025/06/ebu-launches-open-source-mxl-sdk-for-software-based-media-exchange).

## When will MXL v1.0 be published?

(Writing in August 2025) Based on current progress we expect the first release version of the SDK in a few months. As an open source project based on volunteer contribution, this is not possible to commit on hard dates.

## How can I try MXL?

The instructions at https://github.com/dmf-mxl/mxl/blob/main/docs/Building.md cover how to build the SDK (using a VSCode devcontainer or CMake) and then you can use Docker Compose or similar following the instructions at https://github.com/dmf-mxl/mxl/tree/main/examples to run some simple pipelines.

There is also a third party [self-service hands-on tutorial](https://github.com/cbcrc/mxl-hands-on) available to get a first understanding of its functioning.

## What platforms are supported?

The main target is currently Linux on Intel hardware platforms, with some additional notes provided about building on macos.

## What licence(s) does MXL use?

MXL is available under the Apache License Version 2.0. https://www.apache.org/licenses/LICENSE-2.0

## Does MXL have to use containers?

Software media functions do not have to be in containers to use MXL, however the Dynamic Media Facility encourages the containerisation of software media functions for enhanced media operations agility and security.

## Can MXL be used between containers running on different hosts?

MXL has not yet released capability to exchange live media between hosts, but it is on the roadmap.

## How does MXL relate to the VSF Ground-Cloud-Cloud-Ground group?

VSF’s GCCG group has drafted recommendations for interchange and timing of signals to-, from-, and within clouds. MXL’s scope is only the “within” part of this but is informed by VSF’s work and members of the GCCG team are active participants in the MXL project to ensure a consistent approach.

## What approach is being taken to ensure extensibility?

MXL provides a consistent API and data model that allows the underlying transport to be abstracted, for example to use shared host memory, RoCE v2 or a cloud fabric protocol.

## How can I contribute to MXL?

We welcome contributions! Ask questions and notify us of problems via the discussions and issues pages on GitHub. 

To receive information and updates on the releases, meetings, announces, etc, press the "Join our group" button on [this page](https://tech.ebu.ch/dmf/mxl).

See https://github.com/dmf-mxl/mxl/blob/main/CONTRIBUTING.md for more detail about open source licensing, commit sign-off and workflows.

## Are there any MXL events coming soon?

There will be plenty of MXL and DMF activity at IBC 2025 in Amsterdam.  See [this page](https://tech.ebu.ch/mxl) for more information.

