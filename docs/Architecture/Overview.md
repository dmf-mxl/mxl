<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Overview of MXL Architecture

The MXL library provides a mechanism for sharing ring buffers of Flow and Grains (see NMOS IS-04 definitions) between media functions executing on the same host (bare-metal or containerized) and/or between media functions executing on hosts connected using fast fabric technologies (RDMA, EFA, etc).

The MXL library provides APIs for zero-overhead grain _sharing_ using a reader/writer model in opposition to a sender/receiver model. With a reader/writer model, no packetization or even memory copy is involved, preserving memory bandwidth and CPU cycles.
