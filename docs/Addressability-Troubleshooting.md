<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: Troubleshooting

**Problem: "Permission denied" when accessing flow**

Check:
- File permissions on the domain directory
- User/group ownership
- SELinux/AppArmor policies (on Linux)

**Problem: "Flow not found"**

Check:
- UUID is correct (no typos)
- Flow exists in the domain (use `mxl-info -l` to list flows)
- Domain path is correct

**Problem: "Invalid URI"**

Check:
- URI follows the correct format: `mxl:///<path>?id=<uuid>`
- UUID contains hyphens in the correct positions
- No extra characters or whitespace

## Additional resources

- **URI RFC 3986:** https://datatracker.ietf.org/doc/html/rfc3986
- **UUID RFC 4122:** https://datatracker.ietf.org/doc/html/rfc4122
- **mxl-info tool:** See [Tools.md](./Tools.md) for command-line flow inspection

[Back to Addressability overview](./Addressability.md)
