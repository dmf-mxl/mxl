<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Security Policy

MXL is part of the DMF infrastructure, so security must be built into every layer.

MXL relies on the operating system (process separation, permissions, containers, mandatory access control) to isolate media functions. The correct configuration and enforcement of these mechanisms remain the responsibility of the deployment environment.

For MXL Fabrics, connection information and access credentials must be securely managed and exchanged through authenticated, integrity-protected communication channels, using established security protocols such as TLS 1.3 where applicable. Access to fabric resources must be restricted to authorised components through appropriate network segmentation and access controls. RDMA remote keys must not be treated as authentication credentials or relied upon as standalone security mechanisms.

## Reporting a Vulnerability

To report vulnerabilities, you can privately report a potential security issue via the GitHub security vulnerabilities feature. This can be done here:
<https://github.com/dmf-mxl/mxl/security/advisories>

Please do not open a public issue about a potential security vulnerability.
You can find more details on the security vulnerability feature in the GitHub documentation here:
<https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing/privately-reporting-a-security-vulnerability>
