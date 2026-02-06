<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: Best Practices

1. **Use consistent mount paths:** When deploying multiple containers, mount the MXL domain to the same path in all containers (e.g., `/mxl`).

2. **Store URIs in configuration:** Use environment variables or configuration files to pass MXL URIs to applications, not hard-coded paths.

3. **Validate URIs:** Always validate MXL URIs before attempting to open flows. Check for the correct scheme, valid UUID format, and accessible domain path.

4. **Handle errors gracefully:** If a flow ID in the URI doesn't exist, handle the error appropriately (log, retry, fall back to alternative flow).

5. **Document domain paths:** Clearly document where MXL domains are mounted in your deployment environment.

[Back to Addressability overview](./Addressability.md)
