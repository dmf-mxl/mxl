<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Ancillary Data Writing (SMPTE ST 291)

Ancillary data grains contain RFC 8331 payloads (starting from the Length field).

## Complete example

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>
#include <string.h>

// Example: Write a CEA-608 closed caption ANC packet
int write_anc_grain_with_cea608(mxlInstance instance, mxlFlowWriter writer,
                                 const char* caption_text) {
    uint64_t grainIndex = /* ... calculate ... */;

    mxlGrainInfo grainInfo;
    uint8_t* buffer = NULL;

    mxlStatus status = mxlFlowWriterOpenGrain(instance, writer, grainIndex, &grainInfo, &buffer);
    if (status != MXL_STATUS_OK) return -1;

    // Build RFC 8331 payload (MXL format: starts at Length field)
    uint16_t* payload = (uint16_t*)buffer;
    size_t offset = 0;

    // Length (2 bytes): total payload length in words (excluding length field itself)
    // We'll fill this in at the end
    offset += 2;

    // ANC_Count (1 byte): number of ANC packets
    buffer[offset++] = 1;

    // F (1 bit) + reserved (7 bits)
    buffer[offset++] = 0x00;  // F=0 (progressive)

    // ANC packet header (per RFC 8331 section 2.1)
    // C (1 bit) = 0 (luma)
    // Line_Number (11 bits) = 21 (typical for CEA-608)
    // Horizontal_Offset (12 bits) = 0
    uint16_t c_line = (0 << 15) | 21;
    uint16_t h_offset = 0;
    payload[offset / 2] = htons(c_line);
    offset += 2;
    payload[offset / 2] = htons(h_offset);
    offset += 2;

    // S (1 bit) = 0 (luminance)
    // StreamNum (7 bits) = 0
    buffer[offset++] = 0x00;

    // DID (8 bits) = 0x61 (CEA-608)
    buffer[offset++] = 0x61;

    // SID (8 bits) = 0x02
    buffer[offset++] = 0x02;

    // Data_Count (8 bits) = 3 (two caption bytes + checksum)
    uint8_t data_count = 3;
    buffer[offset++] = data_count;

    // User Data Words
    buffer[offset++] = caption_text[0];  // First caption byte
    buffer[offset++] = caption_text[1];  // Second caption byte

    // Checksum (sum of DID, SID, Data_Count, and all data words)
    uint8_t checksum = 0x61 + 0x02 + data_count + caption_text[0] + caption_text[1];
    buffer[offset++] = checksum & 0xFF;

    // Padding to word boundary
    if (offset % 2) {
        buffer[offset++] = 0;
    }

    // Fill in Length field (in 16-bit words, excluding length field itself)
    uint16_t length = (offset - 2) / 2;
    payload[0] = htons(length);

    // Commit the grain
    grainInfo.committedSize = offset;
    grainInfo.originTimestamp = /* ... */;
    status = mxlFlowWriterCommitGrain(instance, writer, &grainInfo);

    return (status == MXL_STATUS_OK) ? 0 : -1;
}
```

## Key points

This example shows CEA-608 captions, but the same pattern applies to other ANC data types (timecode, AFD, etc.). Consult RFC 8331 and SMPTE ST 291 for full packet structure details.

---

Back to [Usage overview](./Usage.md)
