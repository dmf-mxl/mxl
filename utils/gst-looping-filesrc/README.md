<!--
# SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
-->

# gst-looping-filesrc

Like GStreamer's `filesrc` but rewinds file to the beginning once EOF is reached.

Please note that this will only work for video containers that support concatenation like MPEG transport layer (`*.ts`).
Remember to update the pipeline for a input file on your computer!

If you want to loop other containers you either need to seek the whole pipeline or implement the looping logic at the
demuxer-level. The demuxer would need to correctly offset `dts`/`pts` for the repeating segments and rewind the filesrc
to the beginning (or use this implementation that appears as a infinitely big file). The demuxer pulls buffers from
any typ of filesrc.

## Building

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j20
```
To use the built library must be in `GST_PLUGIN_PATH` or place in `/usr/lib/x86_64-linux-gnu/gstreamer-1.0/` or
`/opt/nvidia/deepstream/deepstream/lib/gst-plugins` (when the latter has been enabled for GStreamer plugins).
Using `sudo make install` will install to `/usr/lib/x86_64-linux-gnu/gstreamer-1.0/`.
