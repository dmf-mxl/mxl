<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing: Requirements

For single host MXL environments, any stable time sources such as NTP or SMPTE ST 2059 is adequate.

For multiple hosts MXL environments, MXL requires that the time source of these hosts does not slip against each other over time (the timing source frequency over time shall be the same). Jitter is acceptable and expected - the time stamping model in MXL will describe it properly.

Any NTP locked source that can be traced back to a Stratum 0 time source (atomic clocks/gps fleet) and SMPTE 2059 PTP time source that is properly locked to GPS will provide this guarantee. All cloud providers provide very accurate time sync services (sub millisecond accuracy).

[Back to Timing overview](./Timing.md)
