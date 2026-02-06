<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: Addressing with Containers (Volume Mounts)

When MXL is used in containerized environments, the domain path in the URI refers to the path **inside the container**, not the host path.

## Example scenario

**Host filesystem:**
```
/data/mxl-domain/
```

**Docker run command:**
```bash
docker run -v /data/mxl-domain:/mxl:ro my-reader-app
```

**Container filesystem:**
```
/mxl/
```

**MXL URI (inside container):**
```
mxl:///mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Key point:** The URI uses `/mxl` (the container path), not `/data/mxl-domain` (the host path).

## Kubernetes example

**Pod YAML:**
```yaml
apiVersion: v1
kind: Pod
metadata:
  name: mxl-reader
spec:
  volumes:
  - name: mxl-domain
    hostPath:
      path: /dev/shm/mxl
      type: DirectoryOrCreate
  containers:
  - name: reader
    image: my-mxl-reader:latest
    volumeMounts:
    - name: mxl-domain
      mountPath: /mxl
      readOnly: true
```

**MXL URI (inside pod):**
```
mxl:///mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

The application inside the pod uses `/mxl` regardless of where the volume is mounted on the host.

## Multi-domain scenarios

Multiple domains can coexist on the same host or across hosts.

**Example: Two domains on the same host**

```
mxl:///dev/shm/mxl-live?id=<videoFlowId>
mxl:///dev/shm/mxl-archive?id=<archivedFlowId>
```

**Use case:** Separate domains for live production and archived content.

**Example: Cross-host domains**

```
mxl://host1.local/mxl?id=<flow1>
mxl://host2.local/mxl?id=<flow2>
```

**Use case:** Distributed media processing across multiple servers.

[Back to Addressability overview](./Addressability.md)
