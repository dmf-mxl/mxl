<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MXL Addressability

The following addressing scheme allows applications to refer to one or multiple flows hosted in an MXL domain on a local host or on a remote server.

## Contents

1. [URI Format](./Addressability-URI-Format.md) - RFC 3986 compatible format including authority, domain path, and query component
2. [Structure](./Addressability-Structure.md) - ID parameter structure and examples
3. [UUID Format](./Addressability-UUID-Format.md) - Canonical UUID textual representation
4. [Real-World URI Examples](./Addressability-Real-World-Examples.md) - Local single/multiple flows, domain root, remote flows, IPv6, hostname-based addressing
5. [Addressing with Containers (Volume Mounts)](./Addressability-Containers.md) - Docker, Kubernetes, and multi-domain scenarios
6. [Parsing MXL URIs](./Addressability-Parsing.md) - C and Python parsing examples
7. [URI Translation in Orchestration](./Addressability-Orchestration.md) - Kubernetes ConfigMap and Docker Compose examples
8. [Best Practices](./Addressability-Best-Practices.md) - Recommendations for consistent and robust URI usage
9. [Troubleshooting](./Addressability-Troubleshooting.md) - Common issues and solutions
