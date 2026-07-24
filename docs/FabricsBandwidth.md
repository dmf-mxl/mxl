<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MXL Fabric: Media Payload Calculation

## Goal
Estimate wire bandwidth for MXL media streams across libfabric providers.

## Assumptions and Units
- Media payload math is provider-agnostic.
- Transport overhead math is provider-specific and driven by a provider profile.
- This document starts with one provider profile: `rdma_rocev2`.
- Rates are presented in decimal units (`MB/s = 10^6 B/s`, `GB/s = 10^9 B/s`).

---

## 1) Media Payload Calculation (Protocol-Agnostic)

This section calculates raw media byte rate before adding network or protocol overhead.

### Video (v210)

$$
bytesPerFrame(w,h) = \lceil \frac{w}{48} \rceil \times 128 \times h
$$

Equivalent integer form used in MXL implementation:
- `v210LineBytes = ((w + 47) / 48) * 128`

How to read this formula:
- v210 stores luma/chroma samples in fixed-size groups where each 48-pixel chunk maps to 128 bytes.
- `((w + 47) / 48)` (integer division) gives the number of 48-pixel chunks per scan line.
- Multiplying by `128` gives bytes per scan line.
- Multiplying by `h` gives bytes per frame.

Equivalent form:

$$
bytesPerLine = \lceil \frac{w}{48} \rceil \times 128
$$

$$
bytesPerFrame = bytesPerLine \times h
$$

Example (`1920x1080`):
- `chunksPerLine = (1920 + 47) / 48 = 40`
- `bytesPerLine = 40 \times 128 = 5120`
- `bytesPerFrame = 5120 \times 1080 = 5,529,600`

$$
mediaBytesPerSecond = bytesPerFrame \times fps
$$

Difference between `video/v210` and `video/v210a`:
- `video/v210` carries only the fill image in one plane.
- `video/v210a` carries two planes in one grain: fill (`v210`) plus key (`alpha`).
- `video/v210a` therefore has higher payload than `video/v210` at the same width, height, and frame rate.
- Fill packing is unchanged; the extra cost comes from the alpha plane (`((width + 2) / 3) * 4` bytes per line).

#### Video (v210a)

`video/v210a` contains two planes in one grain: fill (`v210`) and key (`alpha`, packed 10-bit).

$$
v210LineBytes(w) = \lceil \frac{w}{48} \rceil \times 128
$$

$$
alphaLineBytes(w) = \lceil \frac{w}{3} \rceil \times 4
$$

Equivalent integer form used in MXL implementation:
- `alphaLineBytes = ((w + 2) / 3) * 4`

$$
fillBytesPerFrame = v210LineBytes \times h
$$

$$
alphaBytesPerFrame = alphaLineBytes \times h
$$

$$
bytesPerFrame = fillBytesPerFrame + alphaBytesPerFrame
$$

$$
mediaBytesPerSecond = bytesPerFrame \times fps
$$

### Audio (float32 PCM)

$$
bytesPerPacket = channels \times sampleRate \times 4 \times ptime
$$

$$
mediaBytesPerSecond = channels \times sampleRate \times 4
$$

### Data (video/smpte291 in MXL)

MXL sets data grain payload to `4096 bytes` for `video/smpte291`.

$$
bytesPerGrain = 4096
$$

$$
mediaBytesPerSecond = bytesPerGrain \times grainRate
$$

---

## 2) MXL SDK/libfabric Provider Transport Model

This section converts media byte rate into packet rate and adds transport constraints.

### Provider Profile Contract

Use these transport constants per libfabric provider:
- `providerWireOverheadBytes`: per-packet bytes added on wire beyond media payload.
- `providerMtuOverheadBytes`: per-packet bytes that consume MTU budget.
- `effectivePayloadPerPacket = mtu - providerMtuOverheadBytes`.

Units and assumptions:
- All constants in this section are bytes.
- `mtu` is treated as the L3 packet budget used for payload calculations.

### Current Provider Profile: `rdma_rocev2`

Provider constants:
- `providerMtuOverheadBytes = 44`
- `providerWireOverheadBytes = 82`

Provenance of `providerWireOverheadBytes`:
- This constant is a modeled, per-packet on-wire cost for the verbs provider using RoCEv2.
- It is derived from protocol framing bytes that must be transmitted for each packet, not from media payload size.
- The value is assembled from fixed header/trailer fields under the assumptions listed below (no VLAN, no IPv4 options, no IB extension headers).

Derivation:
- `providerMtuOverheadBytes = 44 = IPv4(20) + UDP(8) + BTH(12) + ICRC(4)`
- Additional on-wire-only framing not counted in MTU payload budget:
	- `Preamble/SFD(8) + IFG(12) + Ethernet header(14) + FCS(4) = 38`
- Therefore:
	- `providerWireOverheadBytes = providerMtuOverheadBytes + 38 = 44 + 38 = 82`

Interpretation:
- Use `providerMtuOverheadBytes` to compute payload capacity per packet (`mtu - overhead`).
- Use `providerWireOverheadBytes` to compute true wire-rate amplification (`packetsPerSecond * overhead`).


Profile assumptions:
- Ethernet + IPv4 + UDP + RoCEv2 BTH framing.
- No VLAN tag, no IPv4 options, no InfiniBand extension headers.

### Packetization Formula

For packetized payload streams:

$$
packetsPerEvent = \lceil \frac{payloadBytesPerEvent}{mtu - providerMtuOverheadBytes} \rceil
$$

$$
packetsPerSecond = packetsPerEvent \times eventRate
$$

Where:
- `payloadBytesPerEvent` is payload bytes generated per event:
	- Video: `bytesPerFrame`
	- Audio: `bytesPerPacket` (per `ptime` event)
	- Data: `bytesPerGrain`
- `eventRate` is the number of events per second (`fps`, `1/ptime`, or `grainRate`).

---

## 3) Final Wire Bandwidth Calculation

$$
wireBytesPerSecond = mediaBytesPerSecond + packetsPerSecond \times providerWireOverheadBytes
$$

---

## 4) Worked Examples

All examples in this section use provider `rdma_rocev2` with:
- `providerMtuOverheadBytes = 44`
- `providerWireOverheadBytes = 82`

Naming convention used in examples:
- `payloadBytesPerEvent`: payload bytes generated by one media event.
- `packetsPerEvent`: packets needed for one media event.
- Media-specific aliases are shown in parentheses (for example, `packetsPerFrame`).

### Video Example (`video/v210`, `1920x1080p25`, `mtu=4096`)

Input values:
- `media_type = video/v210`
- `frame_width = 1920`
- `frame_height = 1080`
- `frame_rate = 25`
- `mtu = 4096` (transport assumption used in this document)

Format notes:
- Single fill plane (`v210`), no alpha/key plane.

Media payload:
- `chunksPerLine = (1920 + 47) / 48 = 40`
- `bytesPerLine = 40 * 128 = 5,120`
- `bytesPerFrame = 5,120 * 1080 = 5,529,600`
- `mediaBytesPerSecond = 5,529,600 * 25 = 138,240,000 B/s`

Network/protocol:
- `effectivePayloadPerPacket = 4096 - providerMtuOverheadBytes = 4096 - 44 = 4,052 bytes`
- `payloadBytesPerEvent (bytesPerFrame) = 5,529,600`
- `eventRate (fps) = 25`
- `packetsPerEvent (packetsPerFrame) = (5,529,600 + 4,052 - 1) / 4,052 = 1,365` (integer division, round-up form)
- `packetsPerSecond = packetsPerEvent * eventRate = 1,365 * 25 = 34,125`
- `overheadBytesPerSecond = 34,125 * 82 = 2,798,250 B/s`

Final wire rate:
- `wireBytesPerSecond = 138,240,000 + 2,798,250 = 141,038,250 B/s`
- `wireRate = 141,038,250 / 1,000,000,000 = 0.14103825 GB/s ~= 0.14104 GB/s`


### Video Example (`video/v210a`, `1920x1080p25`, `mtu=4096`)

Input values:
- `media_type = video/v210a`
- `frame_width = 1920`
- `frame_height = 1080`
- `frame_rate = 25`
- `mtu = 4096` (transport assumption used in this document)

Format notes (from upstream MXL):
- Fill plane uses v210 line packing: `v210LineBytes = ((width + 47) / 48) * 128`.
- Key (alpha) plane uses packed 10-bit line layout: `alphaLineBytes = ((width + 2) / 3) * 4`.
- Total frame payload is `fillBytesPerFrame + alphaBytesPerFrame`.

Media payload:
- `v210LineBytes = ((1920 + 47) / 48) * 128 = 40 * 128 = 5,120`
- `alphaLineBytes = ((1920 + 2) / 3) * 4 = 640 * 4 = 2,560`
- `fillBytesPerFrame = 5,120 * 1080 = 5,529,600`
- `alphaBytesPerFrame = 2,560 * 1080 = 2,764,800`
- `bytesPerFrame = fillBytesPerFrame + alphaBytesPerFrame = 5,529,600 + 2,764,800 = 8,294,400`
- `mediaBytesPerSecond = 8,294,400 * 25 = 207,360,000 B/s`

Network/protocol:
- `effectivePayloadPerPacket = 4096 - providerMtuOverheadBytes = 4096 - 44 = 4,052 bytes`
- `payloadBytesPerEvent (bytesPerFrame) = 8,294,400`
- `eventRate (fps) = 25`
- `packetsPerEvent (packetsPerFrame) = (8,294,400 + 4,052 - 1) / 4,052 = 2,047` (integer division, round-up form)
- `packetsPerSecond = packetsPerEvent * eventRate = 2,047 * 25 = 51,175`
- `overheadBytesPerSecond = 51,175 * 82 = 4,196,350 B/s`

Final wire rate:
- `wireBytesPerSecond = 207,360,000 + 4,196,350 = 211,556,350 B/s`
- `wireRate = 211,556,350 / 1,000,000,000 = 0.21155635 GB/s ~= 0.21156 GB/s`

### Audio Example (`2ch`, `48kHz`, `ptime=10ms`, `mtu=4096`)

Input values:
- `media_type = audio/float32`
- `channels = 2`
- `sampleRate = 48,000`
- `ptime = 0.01 s` (`10 ms`)
- `mtu = 4096` (transport assumption used in this document)

Format notes:
- `audio/float32` uses 4 bytes per sample.
- `bytesPerPacket` is payload generated per `ptime` event.

Media payload:
- `mediaBytesPerSecond = 2 * 48,000 * 4 = 384,000 B/s`
- `bytesPerPacket = 2 * 48,000 * 4 * 0.01 = 3,840 bytes`

Network/protocol:
- `effectivePayloadPerPacket = 4096 - providerMtuOverheadBytes = 4096 - 44 = 4,052 bytes`
- `payloadBytesPerEvent (bytesPerPacket) = 3,840`
- `packetsPerEvent (packetsPerPtime) = (3,840 + 4,052 - 1) / 4,052 = 1` (integer division, round-up form)
- `eventRate (ptimeEventsPerSecond) = 1 / 0.01 = 100`
- `packetsPerSecond = packetsPerEvent * eventRate = 1 * 100 = 100`
- `overheadBytesPerSecond = 100 * 82 = 8,200 B/s`

Final wire rate:
- `wireBytesPerSecond = 384,000 + 8,200 = 392,200 B/s`
- `wireRate = 392,200 / 1,000,000 = 0.3922 MB/s ~= 0.39 MB/s`

### Data Example (`grainRate=60`, `mtu=4096`)

Input values:
- `media_type = video/smpte291`
- `bytesPerGrain = 4,096`
- `grainRate = 60`
- `mtu = 4096` (transport assumption used in this document)

Format notes:
- MXL uses fixed data grain payload size of `4,096` bytes for `video/smpte291`.
- One grain is one packetization event for this calculation model.

Media payload:
- `bytesPerGrain = 4,096`
- `mediaBytesPerSecond = bytesPerGrain * 60 = 4,096 * 60 = 245,760 B/s`

Network/protocol:
- `effectivePayloadPerPacket = 4096 - providerMtuOverheadBytes = 4096 - 44 = 4,052 bytes`
- `payloadBytesPerEvent (bytesPerGrain) = 4,096`
- `eventRate (grainRate) = 60`
- `packetsPerEvent (packetsPerGrain) = (4,096 + 4,052 - 1) / 4,052 = 2` (integer division, round-up form)
- `packetsPerSecond = packetsPerEvent * eventRate = 2 * 60 = 120`
- `overheadBytesPerSecond = 120 * 82 = 9,840 B/s`

Final wire rate:
- `wireBytesPerSecond = 245,760 + 9,840 = 255,600 B/s`
- `wireRate = 255,600 / 1,000,000 = 0.2556 MB/s ~= 0.26 MB/s`

---

## Practical Notes
- For audio, wire bandwidth is ptime-sensitive because packet count and per-packet overhead change with ptime, even when raw media byte rate is constant.
- An audio grain duration is the semantical equivalent to what 2110 refers as ptime (packet time)
