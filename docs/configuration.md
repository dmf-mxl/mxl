# Configuration

## Domain level configuration

Domain level configuration is stored in an optional '.options' files stored at the root of the MXL domain.  If present, the MXL SDK will look for specific options defined in the table below and configure itself accordingly.

| Option        | Description                | Default Value |
|----------------|---------------------------|---------------|
| `urn:x-mxl:option:history_duration/v1.0"`         | Depth, in nanoseconds, of a ringbuffer         | 100'000'000ns   |

### Example '.options' file

This options file will configure the depth of the ringbuffers to 500'000'000ns (500ms)

```json
{
    "urn:x-mxl:option:history_duration/v1.0": 500000000
}
```
