/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <gst/base/gstbasesrc.h>
#include <gst/gst.h>
#include <stdio.h>

G_BEGIN_DECLS

#define GST_TYPE_LOOPING_FILESRC (gst_looping_filesrc_get_type())
G_DECLARE_FINAL_TYPE(GstLoopingFileSrc, gst_looping_filesrc, GST,
                     LOOPING_FILESRC, GstBaseSrc)

struct _GstLoopingFileSrc {
  GstBaseSrc basesrc;
  gchar* location;
  bool loop;
  FILE* file;
  guint64 file_size;
};

void gst_looping_filesrc_register_static();

G_END_DECLS
