<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Grain Format: video/smpte291

The `video/smpte291` format is an ancillary data payload based on [RFC 8331](https://datatracker.ietf.org/doc/html/rfc8331#section-2).   Only the bytes starting at the *Length* field (See section 2 of RFC 8331) are stored in the grain (bytes 0 to 13 are redundant in the context of MXL and are not stored).

**Format details:**

- Based on: SMPTE ST 291-1 (Ancillary Data Packet and Space Formatting)
- Transport: RFC 8331 (RTP Payload Format for SMPTE ST 291-1 Ancillary Data)
- MXL storage: Omits RTP header and first 13 bytes of RFC 8331 payload

**SMPTE ST 291 overview:**

SMPTE ST 291 defines how to embed non-video data (timecode, captions, audio metadata, etc.) in the ancillary data space of SDI signals. Common uses include:

- CEA-608/708 closed captions
- SMPTE ST 12 timecode (RP 188)
- Audio control packets
- Active format description (AFD)

**RFC 8331 payload structure:**

```
RFC 8331 Payload (MXL stores from "Length" onward):
┌──────────────────────────────────────────────────┐
│  RTP Header (12 bytes)                           │  NOT stored in MXL
├──────────────────────────────────────────────────┤
│  Extended Sequence Number (2 bytes)              │  NOT stored in MXL
│  Length (2 bytes)                                │  ← MXL grain starts here (offset 0)
│  ANC_Count (1 byte)                              │
│  F (1 bit)                                       │
│  reserved (7 bits)                               │
│  ... (rest of payload as per RFC 8331 sec 2)    │
└──────────────────────────────────────────────────┘
```

MXL grains contain only the portion starting at the Length field. Implementers should consult RFC 8331 section 2 for full payload structure details.

**Typical usage:**

Writers extract ANC packets from SDI or generate them programmatically, format them according to RFC 8331, and write them to MXL grains. Readers parse the RFC 8331 payload and extract individual ANC packets for processing or re-embedding into SDI outputs.

---

[Back to Architecture overview](./Architecture.md)
