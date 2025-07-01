
## Tools

### mxl-info

Simple tool that uses the MXL sdk to open a flow by ID and prints the flow details.

```bash
Usage: ./mxl-info [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  -d,--domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -f,--flow TEXT              The flow id to analyse
  -l,--list                   List all flows in the MXL domain
```

Example 1 : listing all flows in a domain

```bash
./mxl-info -d ~/mxl_domain/ -l
--- MXL Flows ---
        5fbec3b1-1b0f-417d-9059-8b94a47197ed

```

Example 2 : Printing details about a specific flow

```bash
./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed

- Flow [5fbec3b1-1b0f-417d-9059-8b94a47197ed]
        version           : 1
        struct size       : 4192
        flags             : 0x00000000000000000000000000000000
        head index        : 52146817788
        grain rate        : 60000/1001
        grain count       : 3
        last write time   : 2025-02-19 11:44:12 UTC +062314246ns
        last read time    : 1970-01-01 00:00:00 UTC +000000000ns
```

Hint : Live monitoring of a flow details (updates every second)

```bash
watch -n 1 -p ./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

### mxl-viewer

TODO. A generic GUI application based on gstreamer or ffmpeg to display flow(s).

### mxl-gst-videotestsrc

A binary that uses the gstreamer element 'videotestsrc' to produce video grains which will be pushed to a MXL Flow. The video format is configured from a NMOS Flow json file. Here's an example of such file :

```json
{
  "description": "MXL Test File",
  "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ef",
  "tags": {},
  "format": "urn:x-nmos:format:video",
  "label": "MXL Test File",
  "parents": [],
  "media_type": "video/v210",
  "grain_rate": {
    "numerator": 50,
    "denominator": 1
  },
  "frame_width": 1920,
  "frame_height": 1080,
  "colorspace": "BT709",
  "components": [
    {
      "name": "Y",
      "width": 1920,
      "height": 1080,
      "bit_depth": 10
    },
    {
      "name": "Cb",
      "width": 960,
      "height": 1080,
      "bit_depth": 10
    },
    {
      "name": "Cr",
      "width": 960,
      "height": 1080,
      "bit_depth": 10
    }
  ]
}
```

```bash
mxl-gst-videotestsrc
Usage: ./build/Linux-GCC-Release/tools/mxl-gst/mxl-gst-videotestsrc [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  -f,--flow-config-file TEXT REQUIRED
                              The json file which contains the NMOS Flow configuration
  -d,--domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -p,--pattern TEXT [smpte]   Test pattern to use. For available options see https://gstreamer.freedesktop.org/documentation/videotestsrc/index.html?gi-language=c#GstVideoTestSrcPattern
```

### mxl-gst-videosink

A binary that reads from a MXL Flow and display the flow using the gstreamer element 'autovideosink'.

```bash
mxl-gst-videosink
Usage: ./build/Linux-GCC-Release/tools/mxl-gst/mxl-gst-videosink [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  -f,--flow-id TEXT REQUIRED  The flow ID
  -d,--domain TEXT:DIR REQUIRED
                              The MXL domain directory
```
