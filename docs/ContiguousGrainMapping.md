<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Contiguous Grain Mapping

> **Status:** Implemented as an opt-in flow-creation option. The default layout
> remains independent mappings backed by `grains/data.{i}` files.

## 1. Summary

Discrete flows store each grain in its own `grains/data.{i}` file. When the
`contiguousGrains` flow option is enabled, MXL keeps those files but maps them
into adjacent, page-aligned slots in one reserved virtual-address window.

The storage format therefore remains compatible with the layout on `main`:

| Mode | Storage artifacts | Process mapping | Fabrics registrations |
|------|-------------------|-----------------|-----------------------|
| Default | `grains/data.{i}` | Independent addresses | One per grain |
| Contiguous | `grains/data.{i}` | Adjacent fixed-address slots | One per flow when mapping succeeds |

Only virtual contiguity is required. The pages may remain physically
non-contiguous and are faulted in normally.

## 2. Motivation

Registering every grain separately makes registration count scale with
`flows * grains`. DMA frameworks and RDMA providers can impose limits or costs
on those registrations. Mapping a flow's grain files into one contiguous
virtual range allows that range to be registered once while preserving:

- one file and advisory lock per grain;
- existing grain headers and payload offsets;
- per-grain RMA ring slots;
- tools that access individual `grains/data.{i}` files.

The registration benefit comes from virtual-address contiguity, not from using
one backing file.

## 3. Address-space layout

MXL computes a page-aligned slot stride:

```text
logical grain size = sizeof(Grain) + payload size
slot stride        = roundUpToPage(logical grain size)
window size        = grain count * slot stride
```

`ContiguousWindow` reserves the complete range with an inaccessible anonymous
mapping:

```text
[ base ........................................ base + count * stride )
|   slot 0   |   slot 1   |   slot 2   | ... |   slot count - 1   |
```

Each `data.{i}` file is then mapped over slot `i` with `MAP_SHARED |
MAP_FIXED`. Plain `MAP_FIXED` is intentional: each target range is already part
of the process-owned `PROT_NONE` reservation. `MAP_FIXED_NOREPLACE` cannot be
used because it would reject replacement of that reservation.

The reservation uses neither `MAP_POPULATE` nor `MAP_HUGETLB`. It reserves
virtual address space without requiring physically contiguous memory.

## 4. Mapping ownership and bounds

`ContiguousWindow` owns the complete reserved range. Fixed-address
`SharedMemoryInstance<Grain>` objects retain their file descriptors and locks,
but do not individually unmap their slots. Destruction clears the grain
wrappers before releasing the complete window, avoiding double unmapping and
address-reuse gaps.

The fixed-address `SharedMemoryBase` constructor receives both:

```cpp
void* fixedAddr;
std::size_t fixedMappingCapacity;
```

Before calling `mmap`, it rejects a file whose actual size exceeds the slot
capacity. This prevents a malformed or unexpectedly resized grain file from
replacing mappings outside its reserved slot.

The implementation also checks overflow when:

- adding `sizeof(Grain)` to the payload size;
- rounding a logical size to the system page size;
- multiplying grain count by slot stride.

## 5. Flow creation and opening

The layout is requested per flow:

```json
{ "contiguousGrains": true }
```

On successful writer creation, MXL sets
`MXL_FLOW_FLAG_CONTIGUOUS_GRAINS` in the flow metadata. Readers use that flag
to attempt reconstruction of a contiguous mapping without needing the original
flow-creation options.

When opening a flagged flow, MXL:

1. reads the first grain file size and derives the page-aligned stride;
2. verifies that every grain file has the same size;
3. reserves a window for all slots;
4. maps every grain file into its corresponding slot.

The flag records that contiguous mapping was requested for the flow. It does
not guarantee that every process successfully obtained a contiguous window;
the actual local state is reported by `DiscreteFlowData::isWindowMode()`.

Domain-level selection is not implemented by this change. Moving the producer
default into domain configuration remains follow-up work tracked separately.

## 6. Fallback behavior

Contiguous mapping is an optimization. Writers and readers use independent
per-grain mappings when:

- stride calculation fails;
- the address-space reservation fails; or
- a fixed-address file mapping fails.

If a fixed mapping fails after earlier slots were populated, MXL first destroys
all partial grain wrappers and releases the complete window. It then opens or
creates every grain using ordinary kernel-selected mappings. The flow remains
usable, but Fabrics and external DMA integrations cannot assume one contiguous
registration in that process.

Malformed storage is not treated as an optimization failure. Readers reject
inconsistent grain file sizes, files smaller than a grain header, and
unsupported header versions. Fabrics region construction additionally rejects
a header-declared payload size that exceeds the actual grain mapping.

## 7. Fabrics registration

Fabrics keeps one logical `Region` per grain because protocols index regions by
ring slot. Two sizes describe each region:

- `Region::size` is the logical transfer length: grain header plus payload;
- `Region::registrationSize` is the mapped span available for registration.

For an independently mapped grain, both values are the logical size. For a
successfully mapped contiguous grain, `registrationSize` is the actual
page-aligned window stride. This distinction accounts for padding between the
logical end of one grain and the base of the next slot.

`Domain::registerRegions()` coalesces regions only when all regions are in host
memory and each region's base follows the previous registration span exactly.
It validates that:

- every registration span is at least as large as its logical size;
- address-plus-span arithmetic does not overflow;
- the aggregate registration size is representable.

When validation and adjacency checks succeed, Fabrics creates one
`fi_mr_regattr` registration covering the complete window. Each
`RegisteredRegion` shares that registration and retains its own logical length
and offset. Providers using virtual addresses expose each grain's virtual
address; relative-address providers expose its offset in the shared
registration.

If local contiguous mapping fell back, the grains are not adjacent and Fabrics
uses one registration per grain.

## 8. Costs and constraints

Page-rounding each slot does not allocate extra physical pages beyond what
separate file mappings already require. The unused tail consumes only virtual
address space within the slot.

The layout deliberately retains costs associated with per-grain storage:

- one file descriptor and advisory lock per grain;
- one file-backed VMA per grain;
- one inode per grain;
- one address-space reservation per contiguously mapped flow.

The implementation does not force huge pages. Transparent huge-page promotion,
provider behavior, and external DMA mapping policy remain environment-specific.

## 9. Tests

Internal flow and shared-memory tests cover:

- default independent mappings;
- contiguous writer creation and reader reconstruction;
- continued presence of every `grains/data.{i}` file;
- authoritative mapped sizes and page-aligned strides;
- resetting a partially populated window before ordinary mapping fallback;
- inconsistent, truncated, and oversized grain files;
- fixed mapping capacity and size-arithmetic overflow.

Fabrics tests cover:

- exact and padded contiguous regions sharing one memory registration;
- preserved logical lengths and per-region offsets;
- rejection of registration spans smaller than logical regions;
- rejection of address-space overflow.

## 10. Implementation map

| Concern | Files |
|---------|-------|
| Window reservation | `lib/internal/include/mxl-internal/ContiguousWindow.hpp`, `lib/internal/src/ContiguousWindow.cpp` |
| Fixed-address mapping | `lib/internal/include/mxl-internal/SharedMemory.hpp`, `lib/internal/src/SharedMemory.cpp` |
| Grain ownership and bounds | `lib/internal/include/mxl-internal/DiscreteFlowData.hpp`, `lib/internal/src/DiscreteFlowData.cpp` |
| Writer/reader selection and fallback | `lib/internal/src/FlowManager.cpp` |
| Flow option | `lib/internal/include/mxl-internal/FlowOptionsParser.hpp`, `lib/internal/src/FlowOptionsParser.cpp` |
| Fabrics region spans | `lib/fabrics/ofi/src/internal/Region.hpp`, `lib/fabrics/ofi/src/internal/Region.cpp` |
| Single registration | `lib/fabrics/ofi/src/internal/Domain.cpp`, `lib/fabrics/ofi/src/internal/RegisteredRegion.cpp` |

## 11. References

- Issue **#575** - contiguous storage for discrete flows.
- PR **#572** - original contiguous-storage work.
- mxl-requirements **#38** - inter-host memory sharing.
- mxl-requirements **#176** - representing granular flows in one DMA mapping.
