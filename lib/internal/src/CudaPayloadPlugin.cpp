// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/CudaLinearPayloadAllocator.hpp"

#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <mxl/payload_plugin.h>
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/Logging.hpp"

namespace
{
    struct CudaPluginState
    {
        std::unique_ptr<mxl::lib::CudaLinearPayloadAllocator> allocator;
        std::uint64_t grainSize{0};
    };

    mxl::lib::AccessMode accessModeFromPlugin(std::uint32_t mode)
    {
        switch (mode)
        {
            case MXL_PAYLOAD_ACCESS_READ_WRITE: return mxl::lib::AccessMode::READ_WRITE;
            case MXL_PAYLOAD_ACCESS_CREATE_READ_WRITE: return mxl::lib::AccessMode::CREATE_READ_WRITE;
            default: return mxl::lib::AccessMode::READ_ONLY;
        }
    }

    mxlStatus cudaCreate(mxlPayloadBackendCreateInfo const* info, mxlPayloadBackendHandle* outHandle)
    {
        if ((info == nullptr) || (outHandle == nullptr) || (info->struct_size < sizeof(mxlPayloadBackendCreateInfo)))
        {
            return MXL_ERR_INVALID_ARG;
        }
        try
        {
            auto state = std::make_unique<CudaPluginState>();
            state->grainSize = info->logical_payload_size;
            state->allocator = std::make_unique<mxl::lib::CudaLinearPayloadAllocator>(info->device_index, info->local_device_index);
            *outHandle = state.release();
            return MXL_STATUS_OK;
        }
        catch (std::exception const& ex)
        {
            MXL_ERROR("cuda-linear plugin create failed: {}", ex.what());
            return MXL_ERR_UNKNOWN;
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
    }

    void cudaDestroy(mxlPayloadBackendHandle handle)
    {
        delete static_cast<CudaPluginState*>(handle);
    }

    mxlStatus cudaAttach(mxlPayloadBackendHandle handle, mxlPayloadAttachInfo const* info)
    {
        auto* state = static_cast<CudaPluginState*>(handle);
        if ((state == nullptr) || (state->allocator == nullptr) || (info == nullptr) || (info->flow_dir == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        try
        {
            state->grainSize = info->logical_payload_size;
            state->allocator->attach(mxl::lib::GrainPayloadAttachContext{
                .flowDir = info->flow_dir,
                .grainCount = static_cast<std::size_t>(info->grain_count),
                .logicalPayloadSize = static_cast<std::size_t>(info->logical_payload_size),
                .accessMode = accessModeFromPlugin(info->access_mode),
            });
            return MXL_STATUS_OK;
        }
        catch (std::exception const& ex)
        {
            MXL_ERROR("cuda-linear plugin attach failed: {}", ex.what());
            return MXL_ERR_UNKNOWN;
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
    }

    mxlStatus cudaGetView(mxlPayloadBackendHandle handle, std::uint64_t slotIndex, mxlPayloadView* outView)
    {
        auto* state = static_cast<CudaPluginState*>(handle);
        if ((state == nullptr) || (state->allocator == nullptr) || (outView == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        mxl::lib::Grain grain{};
        grain.header.info.grainSize = static_cast<std::uint32_t>(state->grainSize);
        return state->allocator->viewAt(static_cast<std::size_t>(slotIndex), &grain, outView);
    }

    std::uint64_t cudaMappedBytes(mxlPayloadBackendHandle handle)
    {
        auto* state = static_cast<CudaPluginState*>(handle);
        if ((state == nullptr) || (state->allocator == nullptr))
        {
            return 0;
        }
        return state->allocator->mappedPayloadBytes();
    }

    mxlStatus cudaWriteDescriptor(mxlPayloadBackendHandle, char* buffer, std::size_t* inoutSize)
    {
        static char const kDescriptor[] = "{}";
        auto const needed = sizeof(kDescriptor);
        if (inoutSize == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }
        if ((buffer == nullptr) || (*inoutSize < needed))
        {
            *inoutSize = needed;
            return MXL_ERR_INVALID_ARG;
        }
        std::memcpy(buffer, kDescriptor, needed);
        *inoutSize = needed;
        return MXL_STATUS_OK;
    }

    mxlPayloadBackendApiV1 const g_api = {
        .struct_size = sizeof(mxlPayloadBackendApiV1),
        .abi_version = MXL_PAYLOAD_PLUGIN_ABI_VERSION,
        .backend_name = "cuda-linear",
        .create = cudaCreate,
        .destroy = cudaDestroy,
        .attach = cudaAttach,
        .get_view = cudaGetView,
        .mapped_payload_bytes = cudaMappedBytes,
        .write_plugin_descriptor = cudaWriteDescriptor,
    };
}

extern "C" MXL_EXPORT mxlStatus mxlGetPayloadPluginApi(std::uint32_t requestedVersion, mxlPayloadBackendApiV1 const** outApi)
{
    if (outApi == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }
    if (requestedVersion != MXL_PAYLOAD_PLUGIN_ABI_VERSION)
    {
        return MXL_ERR_UNSUPPORTED_OPERATION;
    }
    *outApi = &g_api;
    return MXL_STATUS_OK;
}
