# time.h

Time-related helper functions.

## Functions

### mxlGetCurrentIndex

```c
uint64_t mxlGetCurrentIndex(mxlRational const* editRate);
```

Returns the current ringbuffer index derived from the system TAI time.

- Index 0 corresponds to the epoch defined by SMPTE ST 2059.
- Returns `MXL_UNDEFINED_INDEX` if `editRate` is null or invalid.

See also:
- [rational.h](rational.md)
