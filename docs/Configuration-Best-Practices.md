<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Configuration: Configuration best practices

1. **Start with defaults:** Use the default 200ms history duration unless you have specific requirements.

2. **Measure before tuning:** Profile your application to understand actual latencies before adjusting configuration.

3. **Match batch sizes to processing:** If your reader processes 512 audio samples at a time, configure the writer with maxCommitBatchSizeHint=512.

4. **Consider all readers:** If multiple readers consume a flow, size the history duration for the slowest reader.

5. **Memory budgeting:** Calculate total memory usage before creating flows:
   ```
   Total memory = sum(grain_size * grain_count) for all flows
   ```

6. **Test under load:** Configuration that works for a single flow may not scale to dozens of flows.

[Back to Configuration overview](./Configuration.md)
