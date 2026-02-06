<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: URI Translation in Orchestration

Orchestration systems (Kubernetes, Docker Compose, etc.) may need to translate URIs between different contexts.

## Example: Kubernetes ConfigMap

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: mxl-uris
data:
  video-flow: "mxl:///mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed"
  audio-flow: "mxl:///mxl?id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449"
```

Application pods can read these URIs from the ConfigMap and use them directly, as long as the `/mxl` path is consistently mapped across all pods.

## Example: Docker Compose

```yaml
version: '3.8'
services:
  writer:
    image: my-mxl-writer
    volumes:
      - /dev/shm/mxl:/mxl:rw
    environment:
      - MXL_OUTPUT_URI=mxl:///mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed

  reader:
    image: my-mxl-reader
    volumes:
      - /dev/shm/mxl:/mxl:ro
    environment:
      - MXL_INPUT_URI=mxl:///mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

Both services see the domain at `/mxl`, so the URI is consistent.

[Back to Addressability overview](./Addressability.md)
