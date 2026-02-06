<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Grain Format: video/v210a (Fill + Alpha)

The `video/v210a` format contains both fill and key inside a single grain.  The fill part starts at byte 0 of the grain and follows the v210 definition above. The key buffer is found immediately after the fill buffer.  Samples are organized in blocks of 32 bit values in little-endian.  Each block contains 3 luma samples, one each in bits 0 - 9, 10 - 19 and 20 - 29, the remaining two bits are unused.  The start of each line is aligned to a multiple of 4 bytes, where unused blocks are padded with 0.  The last block of a line might have more padding than just the last 2 bits if the width is not divisible by 3.  For example, 1280x720 resolution has padding for bits 20 to 31 on the last block.

Alpha samples are 'straight' (not premultiplied) and follow the same data range as the fill.  For example, if the fill is _video range_ (64 to 960) then the samples of the key are also _video range_.

**Format details:**

- Fill: Full v210 4:2:2 YCbCr buffer
- Key: Alpha-only buffer using 10-bit luma samples (no chroma)
- Alpha interpretation: Straight alpha (not premultiplied)
- Data range: Matches fill buffer range

**Plane structure:**

```
video/v210a Grain Structure:
┌────────────────────────────────────────────────────────────────┐
│                         FILL BUFFER                            │
│                        (v210 format)                           │
│                   width × height × ~8/3 bytes                  │
├────────────────────────────────────────────────────────────────┤
│                         KEY BUFFER                             │
│                       (alpha channel)                          │
│                   width × height × 10/8 bytes                  │
└────────────────────────────────────────────────────────────────┘
```

**Alpha channel encoding:**

```
32-bit Block Structure (Little-Endian):
┌───────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ 31 30 │ 29 28 27 26 25 24 23 22 21 20 │ 19 18 17 16 15 14 13 12 11 10 │  9  8  7  6  5  4  3  2  1  0 │
├───────┼───────────────────────────────┼───────────────────────────────┼───────────────────────────────┤
│unused │         Luma Sample 2         │         Luma Sample 1         │         Luma Sample 0         │
│  bits │         (bits 20-29)          │         (bits 10-19)          │           (bits 0-9)          │
└───────┴───────────────────────────────┴───────────────────────────────┴───────────────────────────────┘

Line Structure:
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ Block 0 │ Block 1 │ Block 2 │   ...   │ Block N │ Padding │
│ 3 luma  │ 3 luma  │ 3 luma  │         │ 1-3 luma│ (zeros) │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
                                                            ↑
                                                      4-byte aligned

Example (1280x720):
- Width: 1280 pixels
- Blocks per line: ⌈1280/3⌉ = 427 blocks
- Last block: 1 pixel luma sample + zero padding in bits 10-31


Key points:
• Fill buffer (v210) comes first, followed immediately by key buffer (alpha)
• Each 32-bit block contains 3 luma samples (10 bits each)
• Lines are 4-byte aligned with zero padding
```

---

[Back to Architecture overview](./Architecture.md)
