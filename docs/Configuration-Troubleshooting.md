<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Configuration: Troubleshooting configuration issues

**Problem: Readers miss grains (ring buffer overrun)**

Solution: Increase history duration or optimize reader performance.

```json
{
  "urn:x-mxl:option:history_duration/v1.0": 500000000  // Increase from 200ms to 500ms
}
```

**Problem: High memory usage**

Solution: Decrease history duration if readers can keep up.

```json
{
  "urn:x-mxl:option:history_duration/v1.0": 100000000  // Decrease to 100ms
}
```

**Problem: High CPU usage from futex wake-ups**

Solution: Increase batch sizes.

```c
options.maxSyncBatchSizeHint = 64;  // Sync less frequently
```

**Problem: High latency**

Solution: Decrease batch sizes (if CPU allows).

```c
options.maxSyncBatchSizeHint = 1;  // Sync after every grain
```

## Additional resources

- **Flow definition examples:** See `lib/tests/data/` for complete flow JSON examples
- **Writer options API:** See `lib/include/mxl/flow.h` for `mxlFlowWriterOptions` details
- **Reader options API:** See `lib/include/mxl/flow.h` for `mxlFlowReaderOptions` details
- **NMOS IS-04 spec:** https://specs.amwa.tv/is-04/ for standard flow definition fields

[Back to Configuration overview](./Configuration.md)
