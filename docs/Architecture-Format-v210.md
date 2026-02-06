<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Grain Format: video/v210

The `video/v210` format is an uncompressed buffer format carrying 10 bit 4:2:2 video. A detailed description of the format can be found [here](https://wiki.multimedia.cx/index.php/V210) and [here](https://developer.apple.com/library/archive/technotes/tn2162/_index.html#//apple_ref/doc/uid/DTS40013070-CH1-TNTAG8-V210__4_2_2_COMPRESSION_TYPE).

**Format details:**

- Chroma subsampling: 4:2:2 (full vertical resolution, half horizontal chroma resolution)
- Bit depth: 10 bits per component
- Packing: Little-endian 32-bit words, each containing 3 components (Cb Y Cr or Cr Y Cb depending on position)
- Data range: Typically video range (64-960 for luma, 64-960 for chroma) but can be full range (4-1019)

**Scan line layout:**

```
32-bit word structure (little-endian):
┌──────────────────────────────────────────────────────────┐
│ 31-30 │ 29-20   │ 19-10   │  9-0    │
│ unused│   Cr    │   Y0    │   Cb    │  Word 0 (pixel 0-1)
├───────┴─────────┴─────────┴─────────┤
│ unused│   Y2    │   Cb    │   Y1    │  Word 1 (pixel 1-2)
├───────┬─────────┬─────────┬─────────┤
│ unused│   Cb    │   Y3    │   Cr    │  Word 2 (pixel 2-3)
└───────┴─────────┴─────────┴─────────┘
    ... pattern repeats for 4 pixels per 4 words ...
```

Each scan line is padded to 128-byte (32-word) alignment.

**Plane structure:**

v210 is a packed format with all components interleaved. There are no separate planes.

---

[Back to Architecture overview](./Architecture.md)
