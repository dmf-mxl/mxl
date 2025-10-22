<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools

## mxl-info

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

## mxl-viewer

TODO. A generic GUI application based on gstreamer or ffmpeg to display flow(s).

## mxl-gst-videotestsrc

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

## mxl-gst-videosink

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

## mxl-fabrics-demo

This application demonstrates the use of the Fabrics API for host memory transfer operations. Its primary function is to facilitate memory transfers between two nodes. The application does not include an integrated test source. Users must manually start a compatible test source, such as `mxl-gst-videotestsrc`. 
The application consumes an existing Flow and transfers grains to a designated target endpoint as they become available. The target endpoint then generates a new MXL Flow using the received grains.

A typical deployment consists of the following components:
Initiator Node:  
* An `mxl-gst-videotestsrc` as a test source
* An `mxl-fabrics-demo` configured as an initiator

Target Node:
* An `mxl-fabrics-demo` configured as a target
* An `mxl-gst-videosink` to view the result

1- Launch the target `mxl-fabrics-demo` application first:
```sh
mxl-fabrics-demo -d <tmps folder> -f <NMOS JSON file> --node <ip-address> --service <port> --provider <tcp|verbs>
```

Upon successful startup, the target will output TargetInfo to STDOUT as a base64-encoded string. Copy this string for use in step 3.

2- Before launching the initiator, start the test source using the `mxl-gst-videotestsrc` application on the Initiator Node. 

3- Launch the initiator `mxl-fabrics-demo` application, providing the `TargetInfo` string copied from step 1 as a parameter.

```sh
mxl-fabrics-demo -i -d <tmpfs folder> -f <test source Flow UUID> --node <ip-address> --service <port> --provider <tcp|verbs> --target-info <Copied from the target>

```
If everything is running correctly, you should see log messages on the target node:
`[2025-10-07 13:22:59.505] [info] [demo.cpp:503] Comitted grain with index=43996084487 commitedSize=2488320 grainSize=2488320`

```
```
```
```
At this point, you can attach a viewer app (ex: `mxl-gst-videosink`) to the produced Flow on the target side to view the stream.

```sh
mxl-fabrics-demo 
Usage: ./build/Linux-Clang-Debug/tools/mxl-fabrics-demo/mxl-fabrics-demo [OPTIONS]


OPTIONS:
  -h,     --help              Print this help message and exit 
  -d,     --domain TEXT:DIR REQUIRED 
                              The MXL domain directory 
  -f,     --flow TEXT         The flow ID when used as an initiator. The json file which 
                              contains the NMOS Flow configuration when used as a target. 
  -i,     --initiator [0]     Run as an initiator (flow reader + fabrics initiator). If not 
                              set, run as a receiver (fabrics target + flow writer). 
  -n,     --node TEXT         This corresponds to the interface identifier of the fabrics 
                              endpoint, it can also be a logical address. This can be seen as 
                              the bind address when using sockets. 
          --service TEXT      This corresponds to a service identifier for the fabrics 
                              endpoint. This can be seen as the bind port when using sockets. 
  -p,     --provider TEXT [tcp]  
                              The fabrics provider. One of (tcp, verbs or efa). Default is 
                              'tcp'. 
          --target-info TEXT  The target information. This is used when configured as an 
                              initiator . This is the target information to send to.You first 
                              start the target and it will print the targetInfo that you paste 
                              to this argument 
```
```
```
