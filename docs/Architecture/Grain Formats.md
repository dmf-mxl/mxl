<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Grain formats

## Video

Video grains can be of two different formats: video/v210 for video without transparency and video/v210a for fill and key signals (video with alpha transparency).

### video/v210

The `video/v210` format is an uncompressed buffer format carrying 10 bit 4:2:2 video. A detailed description of the format can be found [here](https://wiki.multimedia.cx/index.php/V210) and [here](https://developer.apple.com/library/archive/technotes/tn2162/_index.html#//apple_ref/doc/uid/DTS40013070-CH1-TNTAG8-V210__4_2_2_COMPRESSION_TYPE).

### video/v210a (v210 + alpha)

The `video/v210a` format contains both fill and key inside a single grain.  The fill part starts at byte 0 of the grain and follows the v210 definition above. The key buffer is found immediately after the fill buffer.  Samples are organized in blocks of 32 bit values in little-endian.  Each block contains 3 luma samples, one each in bits 0 - 9, 10 - 19 and 20 - 29, the remaining two bits are unused.  The start of each line is aligned to a multiple of 4 bytes, where unused blocks are padded with 0.  The last block of a line might have more padding than just the last 2 bits if the width is not divisible by 3.  For example, 1280x720 resolution has padding for bits 20 to 31 on the last block.

Alpha samples are 'straight' (not premultiplied) and follow the same data range as the fill.  For example, if the fill is _video range_ (64 to 960) then the samples of the key are also _video range_.

```
video/v210a Grain Structure:
┌──────────────────────────────────────────────────────────────────────────────┐
│                         FILL BUFFER                                         │
│                        (v210 format)                                        │
├──────────────────────────────────────────────────────────────────────────────┤
│                         KEY BUFFER                                          │
│                       (alpha channel)                                       │
└──────────────────────────────────────────────────────────────────────────────┘

32-bit Block Structure (Little-Endian):
┌──────┬───────────────────────┬───────────────────────┬───────────────────────┐
│ 31 30│ 29 28 27 26 25 24 ...│ 19 18 17 16 15 14 ... │  9  8  7  6  5  4 ... │
├──────┼───────────────────────┼───────────────────────┼───────────────────────┤
│unused│   Luma Sample 2       │   Luma Sample 1       │   Luma Sample 0       │
│ bits │   (bits 20-29)        │   (bits 10-19)        │   (bits 0-9)          │
└──────┴───────────────────────┴───────────────────────┴───────────────────────┘

Line Structure:
┌────────┬────────┬────────┬─── ... ───┬────────┬────────┐
│Block 0 │Block 1 │Block 2 │           │Block N │Padding │
│3 luma  │3 luma  │3 luma  │           │1-3 luma│(zeros) │
└────────┴────────┴────────┴─── ... ───┴────────┴────────┘
                                        ↑
                                  4-byte aligned

Example (1280x720):
- Width: 1280 pixels
- Blocks per line: ⎡1280/3⎤ = 427 blocks
- Last block: 1 pixel luma sample + zero padding in bits 10-31

Key points:
• Fill buffer (v210) comes first, followed immediately by key buffer (alpha)
• Each 32-bit block contains 3 luma samples (10 bits each)
• Lines are 4-byte aligned with zero padding
```

## Audio

### audio/float32

The `audio/float32` format has audio stored as 32 bit [IEEE 754](https://standards.ieee.org/ieee/754/6210/) float values with a full-scale range of \[-1.0 ; +1.0]. This is the same audio representation as in RIFF/WAV files with `<wFormatTag>` `0x0003` `WAVE_FORMAT_IEEE_FLOAT`.

!!! note
    Please note that flow producing media functions are not required to stay within the full-scale range and *should not* artificially clamp values to that range. Instead flow consuming media functions that are sensitive to levels exceeding 0 dbFS should, as a fail-safe measure clamp the sample values read to the supported range. This gives operators increased freedom in architecting their processing pipelines and retaining maximum fidelity.

## Ancillary Data

The `video/smpte291` format is an ancillary data payload based on [RFC 8331](https://datatracker.ietf.org/doc/html/rfc8331#section-2).   Only the bytes starting at the *Length* field (See section 2 of RFC 8331) are stored in the grain (bytes 0 to 13 are redundant in the context of MXL and are not stored).
