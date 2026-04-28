<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# EFA Images — Local Build & Run on macOS

Smoke-test the EFA Writer/Reader images on macOS without AWS or EFA hardware.
Uses TCP as the fabric provider (no RDMA). Validates container startup and shared-memory flow exchange.

## Prerequisites

- Docker Desktop installed and running
- Repo checked out on branch `feature/mxl-compile-push-ecr`
- All commands run from the **repo root**

## Step 1: Build the 3 EFA Images

```bash
./examples/build-efa-local.sh
```

Build order: `Dockerfile.efa.base` → `Dockerfile.efa.writer` → `Dockerfile.efa.reader`

> **First run takes ~20 min** (compiles MXL + vcpkg + libfabric from source).
> Subsequent runs use Docker layer cache and complete in seconds.

Images produced:

| Image | Tag |
|-------|-----|
| `ghcr.io/qvest-digital/mxl-base` | `local` |
| `mxl-writer` | `local` |
| `mxl-reader` | `local` |

Verify:
```bash
docker images | grep -E "mxl|ghcr.io/qvest-digital"
```

## Step 2: Run Writer + Reader

```bash
docker compose -f examples/docker-compose.efa.local.yaml up
```

Both containers share a tmpfs volume mounted at `/domain`. The writer generates test
video/audio flows; the reader polls and lists them every 2 seconds.

`FI_PROVIDER=tcp` is set automatically — no EFA device required.

## Step 3: Verify

**Writer is healthy** — logs show `mxl-gst-testsrc` producing frames:
```
writer-1  | [pipeline running...]
```

**Reader is healthy** — logs show `mxl-info` listing flows (UUID `5fbec3b1-...`):
```
reader-1  | Flow: 5fbec3b1-1b0f-417d-9059-8b94a47197ed  format=video ...
```

Check container status:
```bash
docker compose -f examples/docker-compose.efa.local.yaml ps
```

## Stop

```bash
docker compose -f examples/docker-compose.efa.local.yaml down
```

## Known Differences vs. AWS EFA

| | Local (macOS) | AWS EKS |
|---|---|---|
| Fabric provider | `tcp` | `efa` |
| Transport | Shared memory (single node) | RDMA cross-node |
| SRT output | Not tested locally | Via NLB UDP/5000 |
| Image source | `local` tag | ECR `latest` tag |

For full AWS deployment see [EFA_AWS_SETUP.md](aws/EFA_AWS_SETUP.md).

## Troubleshooting

### `lstat /usr/lib/x86_64-linux-gnu: no such file`
Docker is building for ARM (`aarch64`) but the Dockerfile had a hardcoded x86 path.
Fixed in `Dockerfile.efa.base` — rebuild with `./examples/build-efa-local.sh`.

### Container exits immediately with `--domain is required`
The base image sets `ENTRYPOINT ["/bin/bash", "-c"]` which breaks exec-form CMD args.
Fixed in `Dockerfile.efa.writer` and `Dockerfile.efa.reader` with `ENTRYPOINT []`.

### Writer can't find JSON flow files
The writer copies `*.json` from `/usr/share/mxl/data/` into `/home/mxl/` at build time.
If missing, rebuild the base image first: `docker rmi ghcr.io/qvest-digital/mxl-base:local && ./examples/build-efa-local.sh`.
