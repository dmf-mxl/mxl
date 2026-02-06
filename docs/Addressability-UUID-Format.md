<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: UUID Format

A UUID is defined according to the canonical textual representation in lowercase or uppercase hexadecimal:

```
uuid = 8HEXDIG "-" 4HEXDIG "-" 4HEXDIG "-" 4HEXDIG "-" 12HEXDIG
```

Where:

```
HEXDIG = 0–9 or A–F or a–f
```

Hyphens (-) appear only in the fixed canonical positions. No surrounding {} braces are allowed. No URN (urn:uuid:) form is allowed.

[Back to Addressability overview](./Addressability.md)
