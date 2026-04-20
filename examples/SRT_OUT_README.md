<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Notes

## MXL - SRT Output

This use-case, provides minimal changes that need to be done on the custom gstreamer plugin so that Video output can seen over SRT.

### Pre-requisites

* Repeat build process
    * If a previous build was already done, you can also do `ninja all` in the `build/Linux-Clang-Release` directory
* Build containers by running `docker compose --profile '*' build`
* Start new containers by running `docker compose --profile '*' up`
* Do `docker compose --profile '*' down` when ready with testing

### List domains

```bash
# For Docker Compose
docker exec -it examples-reader-media-function-1 /app/mxl-info -d /domain -l

# For Kubernetes
kubectl exec -it deployment/reader-media-function -- /app/mxl-info -d /domain -l
```

### Running mxl-gst-sink

```bash
# Copy desired flow_id from List Domains command
FLOW1_ID=5fbec3b1-1b0f-417d-9059-8b94a47197ed

# For Docker Compose
docker exec -it examples-reader-media-function-1 /app/mxl-gst-sink -d /domain -v $FLOW1_ID

# For Kubernetes
kubectl exec -it deployment/reader-media-function -- /app/mxl-gst-sink -d /domain -v $FLOW1_ID
```

Afterwards, you can view the stream by going to VLC > File > Open Networks and inserting srt://IP:5000?mode=caller

### Gstreamer - SRT Stream

```bash
# For local video
gst-launch-1.0 filesrc location=input.mp4 ! qtdemux ! decodebin ! x264enc tune=zerolatency ! mpegtsmux ! srtsink uri=srt://0.0.0.0:5002?mode=listener

# For docker reader
docker exec -it examples-reader-media-function-1 gst-launch-1.0 filesrc location=input.mp4 ! qtdemux ! decodebin ! x264enc tune=zerolatency ! mpegtsmux ! srtsink uri=srt://0.0.0.0:5000?mode=listener
```

## Miscelaneous

### Useful content for more in-depth tests

[MXL-HANDS-ON Repo](https://github.com/cbcrc/mxl-hands-on)

### Stream SRT using ffmpeg

```bash
ffmpeg -re -stream_loop -1 -i input.mp4   -c:v libx264 -preset ultrafast   -c:a aac   -f mpegts "srt://*:5001?mode=listener

ffmpeg -i "srt://localhost:5000" -t 5 -f null -
```
