<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Addressability: Structure

```
id-param      = "id=" uuid
id-parameters = id-param *( "&" id-param )
```
The `id` name is mandatory for each parameter.

**Examples:**

```
?id=550e8400-e29b-41d4-a716-446655440000
?id=11111111-2222-3333-4444-555555555555&id=aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
```

[Back to Addressability overview](./Addressability.md)
