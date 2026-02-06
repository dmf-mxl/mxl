<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools: mxl-info

A diagnostic tool that inspects MXL domains and flows, providing detailed information about flow configuration and runtime state.

## When would I use this tool?

- **Flow discovery:** List all flows in a domain
- **Debugging:** Check if a flow exists and is active
- **Monitoring:** View flow metadata, grain rates, and latency
- **Cleanup:** Garbage collect stale flows from crashed processes

## Usage

```bash
./mxl-info [OPTIONS] [ADDRESS...]

POSITIONALS:
  ADDRESS TEXT ...            MXL URI

OPTIONS:
  -h,     --help              Print this help message and exit
          --version           Display program version information and exit
  -d,     --domain TEXT:DIR   The MXL domain directory
  -f,     --flow TEXT         The flow id to analyse
  -l,     --list              List all flows in the MXL domain
  -g,     --garbage-collect   Garbage collect inactive flows found in the MXL domain

MXL URI format:
mxl://[authority[:port]]/domain[?id=...]
See: https://github.com/dmf-mxl/mxl/docs/Addressability.md
```

## Examples

**Example 1a: Listing all flows in a domain using command line options**

```bash
./mxl-info -d ~/mxl_domain/ -l
5fbec3b1-1b0f-417d-9059-8b94a47197ed, "MXL Test Flow, 1080p29 with alpha", "Media Function XYZ:Video"
```

**Example 1b: Listing all flows in a domain by specifying an MXL domain URI**

```bash
./mxl-info mxl:///dev/shm/mxl
5fbec3b1-1b0f-417d-9059-8b94a47197ed, "MXL Test Flow, 1080p29 with alpha", "Media Function XYZ:Video"
```

**Example 2a: Printing details about a specific flow using command line options**

```bash
./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed

- Flow [5fbec3b1-1b0f-417d-9059-8b94a47197ed]
                   Version: 1
               Struct size: 2048
                    Format: Video
         Grain/sample rate: 30000/1001
         Commit batch size: 1080
           Sync batch size: 1080
          Payload Location: Host
              Device Index: -1
                     Flags: 00000000
               Grain count: 2

                Head index: 52939144165
           Last write time: 1766402776989915517
            Last read time: 1766402716054676488
          Latency (grains): 1
                    Active: true
```

**Example 2b: Printing details about a specific flow using an MXL URI**

```bash
./mxl-info mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed

- Flow [5fbec3b1-1b0f-417d-9059-8b94a47197ed]
                   Version: 1
               Struct size: 2048
                    Format: Video
         Grain/sample rate: 30000/1001
         Commit batch size: 1080
           Sync batch size: 1080
          Payload Location: Host
              Device Index: -1
                     Flags: 00000000
               Grain count: 2

                Head index: 52939144165
           Last write time: 1766402776989915517
            Last read time: 1766402716054676488
          Latency (grains): 1
                    Active: true
```

**Example 3: Live monitoring of a flow (updates every second)**

```bash
watch -n 1 -p ./mxl-info mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Example 4: Garbage collect stale flows**

```bash
./mxl-info -d /dev/shm/mxl -g
Garbage collected 3 stale flows
```

## Troubleshooting

**Problem: "Domain not found"**

Solution: Check that the domain directory exists and is accessible.

```bash
ls -la /dev/shm/mxl
# Should show .mxl-flow directories
```

**Problem: "Flow not found"**

Solution: List all flows to verify the UUID is correct.

```bash
./mxl-info -d /dev/shm/mxl -l
```

**Problem: "Permission denied"**

Solution: Check file permissions.

```bash
# For writers
chmod 755 /dev/shm/mxl

# For readers
chmod 755 /dev/shm/mxl
chmod -R 644 /dev/shm/mxl/*.mxl-flow/data
```

[Back to Tools overview](./Tools.md)
