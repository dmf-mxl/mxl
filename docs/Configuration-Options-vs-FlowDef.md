<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Configuration: Options.json vs flow_def.json

**options.json:**

- Location: `${mxlDomain}/options.json`
- Scope: Applies to all flows in the domain
- Purpose: Domain-wide defaults (history duration, etc.)
- Optional: If absent, default values are used

**flow_def.json:**

- Location: `${mxlDomain}/${flowId}.mxl-flow/flow_def.json`
- Scope: Applies to a single flow
- Purpose: Describes the format and properties of the flow
- Required: Every flow must have this file

**Example directory structure:**

```
/dev/shm/mxl/
├── options.json                                    # Domain-level config
├── 5fbec3b1-1b0f-417d-9059-8b94a47197ed.mxl-flow/
│   ├── flow_def.json                               # Video flow config
│   ├── data                                        # Flow metadata
│   └── grains/
│       ├── 0
│       ├── 1
│       └── ...
└── b3bb5be7-9fe9-4324-a5bb-4c70e1084449.mxl-flow/
    ├── flow_def.json                               # Audio flow config
    ├── data
    └── channels                                    # Audio sample buffers
```

[Back to Configuration overview](./Configuration.md)
