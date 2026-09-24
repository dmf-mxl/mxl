<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Event flows

An event flow (`MXL_DATA_FORMAT_EVENT`) carries non-periodic, variable size timed events.  An event flow ring buffer behaves like a double ended queue where timing information is carried in the payload itself.  Each ring buffer entry contains either a complete event or a fragment of a large event whose payload is fragmented across multiple events.  Event flows enable, amongst other things, bit accurate round trip of SMPTE ST 2110-41 packets (SMPTE ST 2110-41 RX -> MXL Write -> MXL Read -> SMPTE ST 2110-41 TX).

Each event is identified using a data item type (DIT).  This data item type refers to an external registry, which itself is configurable. Two registry types are currently supported: [SMPTE ST 2110-41 Administrative Register](https://smpte-ra.org/smpte-st2110-41-ar) and the DMF MXL DIT labelling scheme based on URNs. (TODO)

## Creating a flow

Pass an event flow definition to `mxlCreateFlowWriter`. For example:

```json
{
  "id": "cabbc00d-3860-4438-bc48-8ebdfe67305e",
  "format": "urn:x-nmos:format:event",
  "label": "Example events",
  "grain_rate": {"numerator": 1000, "denominator": 1},
  "tags": {"urn:x-nmos:tag:grouphint/v1.0": ["media_function_instance_1:Events"]}
}
```

`urn:x-nmos:format:data.event` is also accepted. Event definitions do not require
`media_type`. The grain rate must be positive and controls queue capacity; it
does not schedule writes or turn timestamps into queue indices.

Capacity is derived from the grain rate and the domain's history duration:

```text
eventCount = historyDurationNs * numerator / (1,000,000,000 * denominator)
```

The default history is 200 ms, so the example above creates 200 slots. The result must be between 2 and 65536.

`mxlEventFlowConfigInfo`, returned in `config.event`, contains `eventCount` and
`eventPayloadSize`. The current flow creation path sets payload capacity to
4096 bytes (`MXL_DATA_FORMAT_GRAIN_SIZE`) per entry.

## Metadata and defaults

| Field | Value after open | Meaning |
| --- | --- | --- |
| `version` | `1` | Metadata version. |
| `size` | `512` | Size of `mxlEventInfo`. |
| `timestamp` | `0` | Application supplied TAI nanoseconds since the epoch. |
| `flags` | `0` | Reserved; must remain zero. |
| `registryType` | `MXL_EVENT_REGISTRY_TYPE_MXL` | Registry for the data item type. |
| `dataItemType[256]` | All bytes zero | Data item type (DIT). |
| `eventSize` | `0` | Actual byte count to publish in this entry. |
| `offset` | `0` | Logical byte offset within a fragmented event. |
| `complete` | `1` | Unfragmented event or final fragment. |
| `reserved[215]` | All bytes zero | Reserved; leave untouched. |
| `index` | `MXL_UNDEFINED_INDEX` | Library-assigned queue position on a successful read; ignored on commit. |

An application must set `timestamp` and the actual `eventSize` before committing.
A zero size publishes an empty event. Payload capacity comes from
`config.event.eventPayloadSize`; opening an event does not clear the staging buffer.

For the SMPTE registry, set `registryType = MXL_EVENT_REGISTRY_TYPE_SMPTE` and
store the DIT string without an `0x` prefix. For the DMF MXL registry use URN formatted strings such as `x-example:message`. The field has a capacity of 256 bytes; readers must bound string access to that array because a producer
can fill it without a terminating NUL.

## Writing and timestamp ordering

Only one event may be open at a time by a writer. Opening returns a private staging
buffer; committing publishes one entry and wakes waiting readers. Cancelling
discards the open write. Opening or cancelling does not advance the queue.

Timestamps must be **nondecreasing** across successful commits in a flow.
Equal timestamps are allowed, including for fragments of the same event. An
earlier timestamp returns `MXL_ERR_INVALID_ARG` without publishing, changing the
head, or changing the last committed timestamp.

An invalid commit leaves the event open so the caller can correct it and retry,
or cancel it. Invalid metadata version, structure size, flags, or a payload size
above capacity also return `MXL_ERR_INVALID_ARG`. The example below cancels on
failure and checks capacity before copying. Its timestamp argument must satisfy
the flow's ordering requirement.

```cpp
#include <cstdint>
#include <cstring>
#include <span>
#include <mxl/flow.h>

mxlStatus writeEvent(mxlFlowWriter writer, mxlFlowConfigInfo const& config, std::uint64_t timestamp,
    std::span<std::uint8_t const> message)
{
    auto event = mxlEventInfo{};
    auto payload = static_cast<std::uint8_t*>(nullptr);
    auto status = mxlFlowWriterOpenEvent(writer, &event, &payload);
    if (status != MXL_STATUS_OK)
    {
        return status;
    }
    if (message.size() > config.event.eventPayloadSize)
    {
        (void)mxlFlowWriterCancelEvent(writer);
        return MXL_ERR_INVALID_ARG;
    }

    event.timestamp = timestamp;
    event.eventSize = static_cast<std::uint32_t>(message.size());
    std::strcpy(event.dataItemType, "x-example:message");
    // OpenEvent already set registryType = MXL, offset = 0 and complete = 1.
    if (!message.empty())
    {
        std::memcpy(payload, message.data(), message.size());
    }
    status = mxlFlowWriterCommitEvent(writer, &event);
    if (status != MXL_STATUS_OK)
    {
        (void)mxlFlowWriterCancelEvent(writer);
    }
    return status;
}
```

## Fragmented events

Split a payload larger than the entry capacity into separate events. Open each
fragment afresh, set its metadata, copy its bytes, then commit. Opening resets
all fields, so set the timestamp, registry, DIT, offset and completion state for
each fragment. Use the same data item type for fragments of the same logical event.

For example, a 4109-byte event can be stored in two entries:

| Entry | `timestamp` | `eventSize` | `offset` | `complete` |
| --- | --- | --- | --- | --- |
| First fragment | `10000` | `4096` | `0` | `0` |
| Last fragment | `10000` | `13` | `4096` | `1` |

An unfragmented entry has `offset = 0` and `complete = 1`. Intermediate fragments
have `complete = 0`. The API publishes and reads each fragment separately; it
does not automatically split, reassemble, or validate fragment continuity.

The consumer tracks the expected next offset using a sufficiently wide integer:
`expectedOffset = std::uint64_t{previous.offset} + previous.eventSize`. Widen
before adding: both metadata fields are 32-bit integers. A first fragment of
size 4096 followed by a fragment at offset 4097 has a gap; offsets 4095 or 0 also
break continuity. Check the event identity as well as the offset. A final
fragment with `complete = 1` only completes an assembly if the preceding
fragments are present and contiguous. A reader may attach in the middle of a
fragmented event or lose fragments to ring overwrite, so it must discard or
otherwise handle incomplete assemblies. Copy fragment bytes before reading the
next entry if they are needed for reassembly.

## Reading and flow lifetime

Create a reader with `mxlCreateFlowReader`, then call
`mxlFlowReaderGetEvent(reader, timeoutNs, &event, &payload)` or
`mxlFlowReaderGetEventNonBlocking(reader, &event, &payload)`. Readers start at the
oldest retained entry and consume entries in queue order. On success, `event.index`
is the zero-based queue position, independent of the timestamp. For consecutive
successful reads, `current.index - previous.index - 1` counts skipped entries.
Compare with `runtime.headIndex` to estimate lag. If the head is
`MXL_UNDEFINED_INDEX` or less than the received index, sample it again: a reader
can observe a published slot before the writer advances the head. Output
arguments remain unchanged on an error.

The payload snapshot remains valid until the next read attempt on that handle or
reader release. Copy any bytes needed beyond that point.

**Each `mxlFlowReader` handle must be accessed by a single thread at a time.** Concurrent
consumers in multiple threads or processes must use separate flow readers, each
with its own cursor. Reader acquisition is cached within an instance, so use a
separate instance for each independent reader of the same flow. Keep fragment
reassembly state with the consumer that owns the reader. Reader handles are process-local; each process must create its own
instance and reader handles.

| Read result | Meaning and next action |
| --- | --- |
| `MXL_STATUS_OK` | One entry was copied; inspect metadata and `eventSize` payload bytes. |
| `MXL_ERR_OUT_OF_RANGE_TOO_EARLY` | No entry became available before the deadline, or none is available for a nonblocking read. |
| `MXL_ERR_OUT_OF_RANGE_TOO_LATE` | The reader lost an entry to overwrite. Its cursor advances past lost data; discard any affected fragment assembly and retry. A retry can encounter another overrun. |
| `MXL_ERR_FLOW_INVALID` | Invalid stored metadata, or no next entry is available and the reader detects a removed/replaced flow. |
| `MXL_ERR_INVALID_ARG` | A metadata or payload output pointer is null. |
| `MXL_ERR_INVALID_FLOW_READER` | The handle is null or belongs to another data format. |
| `MXL_ERR_UNKNOWN` | An internal exception occurred. |

An event flow supports one producer and multiple independently owned readers.
The application must guarantee one writer per flow across all instances and
processes; the library does not enforce this restriction. Callers must serialize
the writer's entire open/edit/commit-or-cancel transaction. Repeated writer
acquisition in one instance reuses its handle.
Coordinate handle release and instance destruction with all active operations.

After a writer crash, a replacement writer may retry a slot whose copy was never
published. If the previous writer completed a slot but died before advancing the
head, reopening adopts that publication, restores its timestamp ordering, and
notifies readers. The next commit uses the following index; an already-published
sequence tag is never reused. This requires the previous writer to have stopped
and the replacement to have sole producer ownership.

## Inspecting events

With the producer running, use `mxl-data-probe`:

```bash
mxl-data-probe --domain /dev/shm/mxl \
    --flow cabbc00d-3860-4438-bc48-8ebdfe67305e --count 3 --timeout-ms 1000
```

The probe prints queue indices, timestamps, flags, registry/DIT, payload size, fragment offset,
completion state and payload bytes in hexadecimal. `--count` counts entries,
including individual fragments, starting with the oldest retained entry. The
printed event number is a local read counter starting at zero; the separate
queue index is the position shared with `headIndex`. The probe displays
fragments separately and does not validate continuity or reassemble them. A
read error, including timeout or overrun, stops it with a nonzero exit status.
`--timeout-ms` applies separately to each read; `--count` defaults to 1 and
`--timeout-ms` defaults to 1000. See [Tools](Tools.md#mxl-data-probe)
for URI usage.
