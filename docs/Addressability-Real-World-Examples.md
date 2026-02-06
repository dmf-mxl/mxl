<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: Real-World URI Examples

## Local single flow

Access a specific flow on the local filesystem:

```
mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Interpretation:**
- Domain: `/dev/shm/mxl`
- Flow: `5fbec3b1-1b0f-417d-9059-8b94a47197ed`

**Use case:** A reader application accessing a video flow written by a local encoder.

## Local multiple flows

Access multiple flows (e.g., video + audio) from the same domain:

```
mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed&id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

**Interpretation:**
- Domain: `/dev/shm/mxl`
- Video flow: `5fbec3b1-1b0f-417d-9059-8b94a47197ed`
- Audio flow: `b3bb5be7-9fe9-4324-a5bb-4c70e1084449`

**Use case:** A player application reading synchronized video and audio.

## Domain root (no specific flows)

Reference the domain without specifying flows (useful for discovery):

```
mxl:///dev/shm/mxl
```

**Interpretation:**
- Domain: `/dev/shm/mxl`
- No specific flows selected

**Use case:** A monitoring tool that enumerates all flows in a domain.

## Remote flow (future feature)

Access a flow on a remote host (requires network protocol support):

```
mxl://10.1.2.3:5000/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Interpretation:**
- Host: `10.1.2.3`
- Port: `5000`
- Domain path: `/mxl` (on the remote host)
- Flow: `5fbec3b1-1b0f-417d-9059-8b94a47197ed`

**Use case:** Accessing flows across hosts in a distributed media processing cluster.

## IPv6 remote flow

```
mxl://[2001:db8::2]:5000/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Interpretation:**
- Host: `2001:db8::2` (IPv6 address)
- Port: `5000`
- Domain path: `/mxl`
- Flow: `5fbec3b1-1b0f-417d-9059-8b94a47197ed`

**Use case:** IPv6-enabled networks in cloud or datacenter environments.

## Hostname-based addressing

```
mxl://media-server.local/mxl-domain?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Interpretation:**
- Host: `media-server.local` (resolved via DNS or mDNS)
- Domain path: `/mxl-domain`
- Flow: `5fbec3b1-1b0f-417d-9059-8b94a47197ed`

**Use case:** Service discovery in a local network using mDNS (Bonjour/Avahi).

[Back to Addressability overview](./Addressability.md)
