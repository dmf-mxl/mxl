/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
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

#include "gst-looping-filesrc.h"
#include <cassert>
#include <algorithm>
#include <gst/base/gstbasesrc.h>
#include <gstreamer-1.0/gst/gstinfo.h>

GST_DEBUG_CATEGORY_STATIC(gst_looping_filesrc_debug);
#define GST_CAT_DEFAULT gst_looping_filesrc_debug

#define gst_looping_filesrc_parent_class parent_class
G_DEFINE_TYPE(GstLoopingFileSrc, gst_looping_filesrc, GST_TYPE_BASE_SRC)

enum
{
    PROP_0,
    PROP_LOCATION,
    PROP_LOOP,
};

#define PACKAGE                     "looping-filesrc"
#define LOOPING_FILESRC_VERSION     "0.1.0"
#define LOOPING_FILESRC_DESCRIPTION "Loop a file"
#define LOOPING_FILESRC_LICENSE     "Apache-2.0"
#define LOOPING_FILESRC_ORIGIN      "http://nvidia.com"
#define LOOPING_FILESRC_SOURCE      "gst-looping-filesrc"

#define LOCATION_DEFAULT ""
#define LOOP_DEFAULT     true

namespace
{

    static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

    void set_property(GObject* object, guint prop_id, GValue const* value, GParamSpec* pspec)
    {
        GST_TRACE_OBJECT(object, __FUNCTION__);

        GstLoopingFileSrc* filesrc = GST_LOOPING_FILESRC(object);

        switch (prop_id)
        {
            case PROP_LOCATION:
                g_free(filesrc->location);
                filesrc->location = g_strdup(g_value_get_string(value));
                break;
            case PROP_LOOP: filesrc->loop = g_value_get_boolean(value); break;
            default:        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        }
    }

    void get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
    {
        GST_TRACE_OBJECT(object, __FUNCTION__);

        GstLoopingFileSrc* filesrc = GST_LOOPING_FILESRC(object);

        switch (prop_id)
        {
            case PROP_LOCATION:
            {
                g_value_set_string(value, filesrc->location);
                break;
            }
            case PROP_LOOP:
            {
                g_value_set_boolean(value, filesrc->loop);
                break;
            }
            default:
            {
                G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
                break;
            }
        }
    }

    void finalize(GObject* object)
    {
        GST_TRACE_OBJECT(object, "finalize");

        auto looping_filesrc = GST_LOOPING_FILESRC(object);
        g_free(looping_filesrc->location);
        if (looping_filesrc->file)
        {
            fclose(looping_filesrc->file);
        }
        G_OBJECT_CLASS(parent_class)->finalize(object);
    }

    gboolean start(GstBaseSrc* basesrc)
    {
        GST_TRACE_OBJECT(basesrc, "start");

        auto* src = GST_LOOPING_FILESRC(basesrc);
        GST_DEBUG_OBJECT(basesrc, "Opening file \"%s\"...", src->location);
        src->file = fopen(src->location, "rb");
        if (!src->file)
        {
            GST_ERROR("Failed to open file \"%s\"", src->location);
            return false;
        }

        fseek(src->file, 0, SEEK_END);
        src->file_size = ftell(src->file);
        fseek(src->file, 0, SEEK_SET);

        gst_base_src_set_dynamic_size(basesrc, false);

        return true;
    }

    gboolean stop(GstBaseSrc* basesrc)
    {
        GST_TRACE_OBJECT(basesrc, "stop");

        auto* src = GST_LOOPING_FILESRC(basesrc);
        if (src->file)
        {
            fclose(src->file);
        }
        src->file = nullptr;
        src->file_size = 0;

        return true;
    }

    gboolean is_seekable(GstBaseSrc* basesrc)
    {
        (void)basesrc;
        return false;
    }

    gboolean get_size(GstBaseSrc* basesrc, guint64* size)
    {
        GstLoopingFileSrc* looping_filesrc = GST_LOOPING_FILESRC(basesrc);

        if (looping_filesrc->loop)
        {
            return false;
        }
        else
        {
            *size = looping_filesrc->file_size;
            return true;
        }
    }

    GstFlowReturn fill(GstBaseSrc* basesrc, guint64 offset, guint size, GstBuffer* buf)
    {
        GstMapInfo info;
        GstLoopingFileSrc* src = GST_LOOPING_FILESRC(basesrc);
        int rtn;
        size_t read = 0;

        GST_TRACE_OBJECT(basesrc, "fill (offset %" G_GUINT64_FORMAT ", size %u)", offset, size);

        if (!gst_buffer_map(buf, &info, GST_MAP_WRITE))
        {
            GST_ERROR("Failed to map buffer");
            return GST_FLOW_ERROR;
        }

        rtn = fread(info.data, 1, size, src->file);
        if (rtn < 0)
        {
            GST_ERROR_OBJECT(basesrc, "Failed to read from file");
            return GST_FLOW_ERROR;
        }

        read += rtn;
        GST_DEBUG_OBJECT(basesrc, "Read %i bytes", rtn);

        if (!src->loop && (guint)rtn != size)
        {
            gst_buffer_unmap(buf, &info);
            gst_buffer_resize(buf, 0, rtn);
            if (rtn == 0)
            {
                GST_DEBUG_OBJECT(basesrc, "End of file");
                return GST_FLOW_EOS;
            }
            else
            {
                return GST_FLOW_OK;
            }
        }

        while (src->loop && read < size)
        {
            size_t diff = size - read;
            GST_DEBUG_OBJECT(basesrc, "Trying to loop file. %zi bytes missing", diff);
            rtn = fseek(src->file, 0, SEEK_SET);
            if (rtn < 0)
            {
                GST_ERROR("Failed to seek to the start of the file: %i", rtn);
                return GST_FLOW_ERROR;
            }

            rtn = fread(info.data + read, 1, diff, src->file);
            if (rtn < 0)
            {
                GST_ERROR_OBJECT(basesrc, "Failed to read from file: %i", rtn);
                return GST_FLOW_ERROR;
            }
            read += rtn;
            GST_DEBUG_OBJECT(basesrc, "Read additional %i bytes", rtn);
        }

        gst_buffer_unmap(buf, &info);
        GST_BUFFER_OFFSET(buf) = offset;
        GST_BUFFER_OFFSET_END(buf) = offset + read;

        return GST_FLOW_OK;
    }
} // namespace

static void gst_looping_filesrc_class_init(GstLoopingFileSrcClass* klass)
{
    GObjectClass* gobject_class;
    GstElementClass* gstelement_class;
    GstBaseSrcClass* gstbasesrc_class;

    gobject_class = (GObjectClass*)klass;
    gstelement_class = (GstElementClass*)klass;
    gstbasesrc_class = (GstBaseSrcClass*)klass;

    gobject_class->finalize = finalize;
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    gstbasesrc_class->start = start;
    gstbasesrc_class->stop = stop;
    gstbasesrc_class->is_seekable = is_seekable;
    gstbasesrc_class->get_size = get_size;
    gstbasesrc_class->fill = fill;

    g_object_class_install_property(gobject_class,
        PROP_LOCATION,
        g_param_spec_string("location", "Location of file to loop", "Location of file to loop", LOCATION_DEFAULT, G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class,
        PROP_LOOP,
        g_param_spec_boolean("loop", "Whether to loop the file", "Whether to loop the file", LOOP_DEFAULT, G_PARAM_READWRITE));

    gst_element_class_add_static_pad_template(gstelement_class, &srctemplate);
    gst_element_class_set_details_simple(gstelement_class,
        "looping-filesrc",
        "Generic",
        "Like filesrc but will loop the input content",
        "NVIDIA Corporation. See "
        "https://developer.nvidia.com/holoscan-for-media");
}

static void gst_looping_filesrc_init(GstLoopingFileSrc* filesrc)
{
    auto basesrc = GST_BASE_SRC(filesrc);

    filesrc->location = nullptr;
    filesrc->file = nullptr;
    filesrc->file_size = 0;
    filesrc->loop = LOOP_DEFAULT;

    gst_base_src_set_live(basesrc, false);
    gst_base_src_set_automatic_eos(basesrc, false);
    g_object_set(filesrc, "location", LOCATION_DEFAULT, NULL);
}

static gboolean plugin_init(GstPlugin* plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_looping_filesrc_debug, "looping_filesrc", 0, "Looping filesrc plugin");

    return gst_element_register(plugin, "looping_filesrc", GST_RANK_NONE, gst_looping_filesrc_get_type());
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, looping_filesrc, LOOPING_FILESRC_DESCRIPTION, plugin_init, LOOPING_FILESRC_VERSION,
    LOOPING_FILESRC_LICENSE, PACKAGE, LOOPING_FILESRC_ORIGIN)

void gst_looping_filesrc_register_static()
{
    gst_plugin_register_static(GST_VERSION_MAJOR,
        GST_VERSION_MINOR,
        "looping_filesrc",
        LOOPING_FILESRC_DESCRIPTION,
        plugin_init,
        LOOPING_FILESRC_VERSION,
        LOOPING_FILESRC_LICENSE,
        LOOPING_FILESRC_SOURCE,
        PACKAGE,
        LOOPING_FILESRC_ORIGIN);
}
