<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Discrete Grain I/O

Grain I/O can be 'partial'. In other words, a FlowWriter can write the bytes of a grain progressively (as slices for example).

The important field here is the `mxlGrainInfo.committedSize`: it tells us how many bytes in the grain are valid. A complete grain will have `mxlGrainInfo.committedSize == mxlGrainInfo.grainSize`. It is imperative that FlowReaders _always_ take into consideration the `mxlGrainInfo.committedSize` field before making use of the grain buffer. The `mxlFlowReaderGetGrain` function will return as soon as new data is committed to the grain.

For a working code example, see the [Practical Examples](./Architecture-Practical-Examples.md#partial-grain-io-sliced-writes) document.

---

[Back to Architecture overview](./Architecture.md)
