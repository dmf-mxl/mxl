// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * Stable C ABI for grain-payload backends loaded with dlopen.
 *
 * A plugin exports one symbol, mxlGetPayloadPluginApi. The host never calls
 * C++ across the boundary. Built-in backends (host, placeholder) are not
 * loaded this way and cannot be replaced by a plugin of the same name.
 * cuda-linear is itself this kind of plugin (libmxl-payload-cuda.so), loaded
 * from the directory that contains libmxl when a flow asks for it.
 *
 * Library paths come from mxlLoadPayloadPlugin, the instance option
 * "payloadPlugins", or MXL_PAYLOAD_PLUGIN_PATH. payload.json may name a
 * backend; it must not name a filesystem path.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <mxl/flowinfo.h>
#include <mxl/mxl.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MXL_PAYLOAD_PLUGIN_ABI_VERSION 1u

#define MXL_PAYLOAD_ACCESS_READ_ONLY 0
#define MXL_PAYLOAD_ACCESS_READ_WRITE 1
#define MXL_PAYLOAD_ACCESS_CREATE_READ_WRITE 2

    typedef void* mxlPayloadBackendHandle;

    typedef struct mxlPayloadBackendCreateInfo
    {
        uint32_t struct_size;
        int32_t location;
        int32_t device_index;
        /** -1 means the owner's device_index. */
        int32_t local_device_index;
        uint64_t logical_payload_size;
    } mxlPayloadBackendCreateInfo;

    typedef struct mxlPayloadAttachInfo
    {
        uint32_t struct_size;
        uint32_t access_mode;
        uint64_t grain_count;
        uint64_t logical_payload_size;
        /** UTF-8 path of the flow directory. Never null on a successful host call. */
        char const* flow_dir;
        /**
         * JSON object text from payload.json "plugin", or null when the flow is
         * being created or the field is absent. The host owns this string only
         * for the duration of attach.
         */
        char const* plugin_json;
    } mxlPayloadAttachInfo;

    typedef struct mxlPayloadBackendApiV1
    {
        uint32_t struct_size;
        uint32_t abi_version;
        /** Stable backend name, e.g. "vendor.cuda". Must not be a built-in name. */
        char const* backend_name;

        mxlStatus (*create)(mxlPayloadBackendCreateInfo const* info, mxlPayloadBackendHandle* out_handle);
        void (*destroy)(mxlPayloadBackendHandle handle);
        mxlStatus (*attach)(mxlPayloadBackendHandle handle, mxlPayloadAttachInfo const* info);
        mxlStatus (*get_view)(mxlPayloadBackendHandle handle, uint64_t slot_index, mxlPayloadView* out_view);
        uint64_t (*mapped_payload_bytes)(mxlPayloadBackendHandle handle);

        /**
         * Optional. Writes a JSON object (not a full payload.json) describing
         * plugin-owned slot metadata.
         *
         * Pass buffer NULL to query the required size, including the trailing NUL.
         * On success the host embeds the object under payload.json "plugin".
         */
        mxlStatus (*write_plugin_descriptor)(mxlPayloadBackendHandle handle, char* buffer, size_t* inout_size);
    } mxlPayloadBackendApiV1;

    typedef mxlStatus (*mxlGetPayloadPluginApiFn)(uint32_t requested_version, mxlPayloadBackendApiV1 const** out_api);

    /**
     * Plugin entry point. Requested version is MXL_PAYLOAD_PLUGIN_ABI_VERSION.
     * Return MXL_ERR_UNSUPPORTED_OPERATION if the version is unknown.
     */
    MXL_EXPORT
    mxlStatus mxlGetPayloadPluginApi(uint32_t requested_version, mxlPayloadBackendApiV1 const** out_api);

    /**
     * Load a payload plugin into the process-wide registry.
     *
     * The same path is ignored on a second load. A second plugin that claims a
     * backend name already registered by another library fails. Libraries stay
     * mapped for the life of the process.
     *
     * \param instance A live MXL instance. The registry itself is process-wide.
     * \param path Absolute or relative path to a shared library exporting mxlGetPayloadPluginApi.
     */
    MXL_EXPORT
    mxlStatus mxlLoadPayloadPlugin(mxlInstance instance, char const* path);

#ifdef __cplusplus
}
#endif
