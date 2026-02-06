<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Video Grain with Alpha (v210a)

v210a grains contain both fill (RGB) and key (alpha) buffers.

## Complete example

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>

size_t calculate_v210a_grain_size(uint32_t width, uint32_t height) {
    // Fill buffer (v210)
    size_t fill_line_bytes = ((width * 8 + 2) / 3);
    fill_line_bytes = ((fill_line_bytes + 127) / 128) * 128;
    size_t fill_size = fill_line_bytes * height;

    // Key buffer (alpha only, 10-bit luma)
    // 3 alpha samples per 32-bit word
    size_t key_line_words = (width + 2) / 3;  // Round up
    key_line_words = ((key_line_words * 4 + 3) / 4) * 4;  // 4-byte align
    size_t key_size = key_line_words * 4 * height;

    return fill_size + key_size;
}

int write_v210a_grain(mxlInstance instance, mxlFlowWriter writer,
                      uint32_t width, uint32_t height) {
    // Get grain index (same as v210 example)
    uint64_t grainIndex = /* ... calculate ... */;

    mxlGrainInfo grainInfo;
    uint8_t* buffer = NULL;

    mxlStatus status = mxlFlowWriterOpenGrain(instance, writer, grainIndex, &grainInfo, &buffer);
    if (status != MXL_STATUS_OK) return -1;

    // Calculate buffer layout
    size_t fill_line_bytes = ((width * 8 + 2) / 3);
    fill_line_bytes = ((fill_line_bytes + 127) / 128) * 128;
    size_t fill_size = fill_line_bytes * height;

    uint8_t* fill_buffer = buffer;
    uint8_t* key_buffer = buffer + fill_size;

    // Fill the fill buffer (v210 format)
    uint32_t* fill_pixels = (uint32_t*)fill_buffer;
    size_t fill_words = fill_size / 4;
    for (size_t i = 0; i < fill_words; i++) {
        // Write v210 pixels (see v210 example)
        fill_pixels[i] = /* ... v210 data ... */;
    }

    // Fill the key buffer (alpha channel)
    uint32_t* key_words = (uint32_t*)key_buffer;
    size_t key_line_words = (width + 2) / 3;

    for (uint32_t line = 0; line < height; line++) {
        for (uint32_t word_idx = 0; word_idx < key_line_words; word_idx++) {
            uint32_t pixel_idx = line * width + word_idx * 3;

            // Get 3 alpha samples (10-bit each)
            uint32_t alpha0 = (pixel_idx + 0 < width * height) ? 960 : 0;  // Full opacity
            uint32_t alpha1 = (pixel_idx + 1 < width * height) ? 960 : 0;
            uint32_t alpha2 = (pixel_idx + 2 < width * height) ? 960 : 0;

            // Pack into 32-bit word: [unused(2)][alpha2(10)][alpha1(10)][alpha0(10)]
            uint32_t packed = (alpha2 << 20) | (alpha1 << 10) | alpha0;
            key_words[line * key_line_words + word_idx] = packed;
        }
    }

    // Commit the complete grain
    grainInfo.committedSize = grainInfo.grainSize;
    grainInfo.originTimestamp = /* ... */;
    status = mxlFlowWriterCommitGrain(instance, writer, &grainInfo);

    return (status == MXL_STATUS_OK) ? 0 : -1;
}
```

## Key points

- The fill buffer comes first, followed immediately by the key buffer.
- Alpha values use the same data range as luma (64-960 for video range).
- Alpha is straight (not premultiplied).
- Each 32-bit word in the key buffer contains 3 alpha samples (10 bits each).

---

Back to [Usage overview](./Usage.md)
