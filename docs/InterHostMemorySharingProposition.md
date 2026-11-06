<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

## Disclaimer

The following text refers to a specific vendor; however, this reference is made purely for informational clarity. It should not be interpreted as an endorsement or indication of bias.

# Inter-node memory sharing

For memory sharing by copy (internode host-device, etc.), we intend to use `libfabric` which supports transfers of heterogenous memories very well. Although not in technical scope of this document, device to host and device to device memory sharing has support in the Libfabric framework. It also supports CUDA quite well through the HMEM memory registration. It provides support for GPUDirect RDMA by using either `nvidia-peermem` kernel module or `dma-buf`, when not only targeting NVIDIA devices (need to confirm with Intel's MXL member).

## Memory Models

### Memory sharing by shared access

The current implementation of the MXL flow shares memory by shared access. No data movement is involved at all. For a DiscreteFlow, the FlowWriter signals to subscribers (FlowReader's) when a new grain is available. These readers get read-only access to the newly ready grain to consume. The workflow follows these steps:

1. A flow is created (allocating memory for the buffer).
2. The FlowWriter opens a grain with the OpenGrain method
3. The FlowWriter fills the grain
4. The FlowWriter commits the grain with the Commit method

Meanwhile the FlowWriter does it job, the FlowReaders will query grains with the GetGrain method. See schematize representation below.

![publish-subscribe svg drawio](https://github.com/user-attachments/assets/ae2adb28-0920-448d-ae65-47842ef535c7)


This memory model is only possible in the case where both the publisher and subscriber are in the same memory domain. For example, 2 host processes that have access to the same tmpfs folder. This includes sharing host memory, but also GPU memory if both processes target the same GPU.

## Memory sharing by copy (or movement)

When the producer is in a different memory domain than the subscriber, we can't use the same memory sharing by access scheme. In that case, we need to copy memory from one domain to another domain. This includes:
* Host to Host transfer in in the same host, but different domain
* Host to Host transfer in different hosts (internode)
* Host to/from Device transfer
* Device to/from Device transfer intranode and internode

In this scheme we refer to an **initiator** as the entity that initiate the transfer of its memory and the **target** as the entity that receives the memory.

To copy a buffer from an initiator to a target, we have two choices
1. Send/Receive Transfers
This method uses two-ways transfer which requires that the target posts a receive request and the initiator posts a send request. Normally, to avoid having a bouncing buffer, a tag is used so that the received data from the NIC end-up in the appropriate buffer. The implication of this is that at connection and throughout the session the initiator must communicate the next grains or samples to expect.

2. Remote Memory Writes: 
This method consists of the initiator directly modifying the target's memory without the target even being involved. An optional notification can be issued to the target once the transfer is completed. This approach eliminates the need for maintaining synchronization, but requires an initial setup phase, including memory registration and the exchange of access credentials.

The workflow for the remote memory write scheme would be as follows:

1. Targets are created
2. The targets register the receiving buffer memory region
3. The targets generate the `TargetInfo` object which should contain the setup URL(endpoint address: “{provider}://{node}:{service}”), the remote keys (for remote memory access) and the virtual addresses of the target buffers. That is done using `mxlFabricsTargetSetup()`
4. The `TargetInfo` is sent to the initiator by some out-of-band method.
5. The initator registers the targets using `mxlFabricsInitiatorAddTarget` the `TargetInfo` object.
6. The initiator can then transfer grains by using `mxlFabricsInitiatorTransferGrain` to transfer to all targets. Under the hood the library will use the libfabric write method, and the address will be calculated using:
   - entry = grainIndex % numberOfGrains

7. On the target side, the target can inspect completion of a specific `grainIndex` with `mxlFabricsTargetWaitForNewGrain` (blocking) or `mxlFabricsTargetTryNewGrain` (non-blocking)

![point-to-point svg drawio](https://github.com/user-attachments/assets/30cb30ca-186e-48ed-aad6-8b2f1373f505)


Our proposal introduces a new interface, referred to as the "fabrics" interface with the goal of supporting all the "memory sharing by copy" use-cases.

Link to the fabrics API draft (https://git.ebu.io/dmf/mxl/-/blob/feature/fabrics-api/lib/include/mxl/fabrics.h?ref_type=heads)

## Libfabric API

One possible implementation of that interface is to use `libfabric`. The intent is to only use a subset (what we need) of the libfabric API.

### Provider Selection

MXL should provide a simple call where the user will provide a subset of parameters that allows to query providers and select the first provider that matches.

Parameters :

- bind address / service (port)
- Provider type
- Transmit completion queue size

### Address Vector

Address vector is a record of destination endpoints we can operate on. This is used in the case of connection-less endpoints (EFA)

- Add/remove libfabric addresses in the address vector. We need to book-keep addresses we add/remove.
- Provide a function to query the libfabric address.

### Operations

- MXL should provide an API to allow a user to do remote WRITE operations on a endpoint. We should probably only expose scatter-gather version (quite useful for audio).
- Provide functions to register of different types of buffers (host, cuda, etc.)
- For completion, the fabrics API will provide blocking and non-blocking functions

### Demo/Reference application
To showcase a usage example of the new API, we should provide a proxy that enables communication between the MXL flow domain and the MXL fabrics domain, in both directions.

Specifically, the demo application will allow a user to expose an MXL flow to a Fabrics endpoint. Within the application, the user will be able to invoke two components: an initiator and a target. Each initiator and target will be capable of handling a single MXL flow.

The initiator consists of an MXL Flow Reader and a Fabrics Initiator endpoint, while the target is composed of a Fabrics Target endpoint and a MXL Flow Writer. The initiator is responsible for transferring grains or samples while operating asynchronously, transferring grains or samples as soon as they are available.

Below is a diagram of the demo app.

![demo-app svg drawio](https://github.com/user-attachments/assets/b67c924d-2422-4924-ad28-d524ff8ed49e)


# Initial API Draft

Draft of API (https://github.com/dmf-mxl/mxl/blob/feature/fabrics-api/lib/include/mxl/fabrics.h) 

Demo application (https://github.com/dmf-mxl/mxl/blob/feature/fabrics-api/tools/mxl-fabrics-sample/demo.cpp)

# References

1. hmem_cuda (libfabric/src/hmem_cuda.c at 66fd5a38e35b5f0cc0f044e9e99f57e1531c49e1 · ofiwg/libfabric)
2. hmem (libfabric/src/hmem.c at 66fd5a38e35b5f0cc0f044e9e99f57e1531c49e1 · ofiwg/libfabric)
3. Buffer Sharing and Synchronization (dma-buf) — The Linux Kernel documentation(https://docs.kernel.org/driver-api/dma-buf.html)
4. GPU Direct (https://developer.download.nvidia.com/CUDA/training/cuda_webinars_GPUDirect_uva.pdf)
5. cuda (https://docs.nvidia.com/cuda/cuda-c-programming-guide/#interprocess-communication)