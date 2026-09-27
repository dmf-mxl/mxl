// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <mxl/payload_plugin.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestBytesBackend
{
    uint64_t grainSize;
    uint64_t grainCount;
    int32_t deviceIndex;
    uint8_t** slots;
    int ownsSlots;
} TestBytesBackend;

typedef struct PublishedFlow
{
    char* flowDir;
    uint64_t grainCount;
    uint64_t grainSize;
    uint8_t** slots;
} PublishedFlow;

static PublishedFlow g_published[8];

static PublishedFlow* findPublished(char const* flowDir)
{
    for (size_t i = 0; i < 8; ++i)
    {
        if ((g_published[i].flowDir != NULL) && (strcmp(g_published[i].flowDir, flowDir) == 0))
        {
            return &g_published[i];
        }
    }
    return NULL;
}

static PublishedFlow* takePublishedSlot(void)
{
    for (size_t i = 0; i < 8; ++i)
    {
        if (g_published[i].flowDir == NULL)
        {
            return &g_published[i];
        }
    }
    return NULL;
}

static mxlStatus testCreate(mxlPayloadBackendCreateInfo const* info, mxlPayloadBackendHandle* out_handle)
{
    if ((info == NULL) || (out_handle == NULL) || (info->struct_size < sizeof(mxlPayloadBackendCreateInfo)))
    {
        return MXL_ERR_INVALID_ARG;
    }
    TestBytesBackend* backend = calloc(1, sizeof(TestBytesBackend));
    if (backend == NULL)
    {
        return MXL_ERR_UNKNOWN;
    }
    backend->grainSize = info->logical_payload_size;
    backend->deviceIndex = info->local_device_index >= 0 ? info->local_device_index : info->device_index;
    *out_handle = backend;
    return MXL_STATUS_OK;
}

static void testDestroy(mxlPayloadBackendHandle handle)
{
    TestBytesBackend* backend = handle;
    if (backend == NULL)
    {
        return;
    }
    if ((backend->ownsSlots != 0) && (backend->slots != NULL))
    {
        for (uint64_t i = 0; i < backend->grainCount; ++i)
        {
            free(backend->slots[i]);
        }
        free(backend->slots);
    }
    free(backend);
}

static mxlStatus testAttach(mxlPayloadBackendHandle handle, mxlPayloadAttachInfo const* info)
{
    TestBytesBackend* backend = handle;
    if ((backend == NULL) || (info == NULL) || (info->flow_dir == NULL))
    {
        return MXL_ERR_INVALID_ARG;
    }
    if (backend->slots != NULL)
    {
        return MXL_STATUS_OK;
    }

    if (info->access_mode != MXL_PAYLOAD_ACCESS_CREATE_READ_WRITE)
    {
        PublishedFlow* published = findPublished(info->flow_dir);
        if (published == NULL)
        {
            return MXL_ERR_FLOW_NOT_FOUND;
        }
        backend->grainCount = published->grainCount;
        backend->grainSize = published->grainSize;
        backend->slots = published->slots;
        backend->ownsSlots = 0;
        return MXL_STATUS_OK;
    }

    backend->grainCount = info->grain_count;
    backend->grainSize = info->logical_payload_size;
    backend->ownsSlots = 1;
    backend->slots = calloc(info->grain_count == 0 ? 1 : (size_t)info->grain_count, sizeof(uint8_t*));
    if (backend->slots == NULL)
    {
        return MXL_ERR_UNKNOWN;
    }
    for (uint64_t i = 0; i < info->grain_count; ++i)
    {
        backend->slots[i] = calloc(1, (size_t)info->logical_payload_size);
        if (backend->slots[i] == NULL)
        {
            return MXL_ERR_UNKNOWN;
        }
    }

    PublishedFlow* slot = takePublishedSlot();
    if (slot == NULL)
    {
        return MXL_ERR_UNKNOWN;
    }
    slot->flowDir = malloc(strlen(info->flow_dir) + 1);
    if (slot->flowDir == NULL)
    {
        return MXL_ERR_UNKNOWN;
    }
    memcpy(slot->flowDir, info->flow_dir, strlen(info->flow_dir) + 1);
    slot->grainCount = backend->grainCount;
    slot->grainSize = backend->grainSize;
    slot->slots = backend->slots;
    return MXL_STATUS_OK;
}

static mxlStatus testGetView(mxlPayloadBackendHandle handle, uint64_t slot_index, mxlPayloadView* out_view)
{
    TestBytesBackend* backend = handle;
    if ((backend == NULL) || (out_view == NULL) || (backend->slots == NULL) || (slot_index >= backend->grainCount))
    {
        return MXL_ERR_INVALID_ARG;
    }
    memset(out_view, 0, sizeof(*out_view));
    out_view->kind = MXL_PAYLOAD_KIND_HOST_PTR;
    out_view->grainSize = (uint32_t)backend->grainSize;
    out_view->deviceIndex = backend->deviceIndex;
    out_view->u.hostPtr = backend->slots[slot_index];
    return MXL_STATUS_OK;
}

static uint64_t testMappedBytes(mxlPayloadBackendHandle handle)
{
    (void)handle;
    return 0;
}

static mxlStatus testWriteDescriptor(mxlPayloadBackendHandle handle, char* buffer, size_t* inout_size)
{
    static char const kDescriptor[] = "{\"marker\":\"test-bytes\"}";
    size_t const needed = sizeof(kDescriptor);
    (void)handle;
    if (inout_size == NULL)
    {
        return MXL_ERR_INVALID_ARG;
    }
    if ((buffer == NULL) || (*inout_size < needed))
    {
        *inout_size = needed;
        return MXL_ERR_INVALID_ARG;
    }
    memcpy(buffer, kDescriptor, needed);
    *inout_size = needed;
    return MXL_STATUS_OK;
}

static mxlPayloadBackendApiV1 const g_api = {
    .struct_size = sizeof(mxlPayloadBackendApiV1),
    .abi_version = MXL_PAYLOAD_PLUGIN_ABI_VERSION,
    .backend_name = "test-bytes",
    .create = testCreate,
    .destroy = testDestroy,
    .attach = testAttach,
    .get_view = testGetView,
    .mapped_payload_bytes = testMappedBytes,
    .write_plugin_descriptor = testWriteDescriptor,
};

mxlStatus mxlGetPayloadPluginApi(uint32_t requested_version, mxlPayloadBackendApiV1 const** out_api)
{
    if (out_api == NULL)
    {
        return MXL_ERR_INVALID_ARG;
    }
    if (requested_version != MXL_PAYLOAD_PLUGIN_ABI_VERSION)
    {
        return MXL_ERR_UNSUPPORTED_OPERATION;
    }
    *out_api = &g_api;
    return MXL_STATUS_OK;
}
