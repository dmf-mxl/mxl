// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file TestMemfdBackend.cpp
 *
 * A payload backend for tests that shares its storage through file descriptors, the way a device backend shares
 * dma-bufs or exported device memory, but without a device. Each slot is held in its own memory file, created with
 * memfd_create(). The backend defines its own storage type, described in TestMemfdStorage.hpp, and has the capability
 * to share descriptors, so that libmxl serves them on payload.sock and readers receive them.
 *
 * The backend stores video/v210 flows, whose media type it reads from the flow definition, and accepts no options.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <picojson/wrapper.h>
#include <mxl/platform.h>
#include "mxl-internal/PayloadBackendAbi.h"
#include "TestMemfdStorage.hpp"

namespace
{
    /** State of one flow in one process. */
    struct Instance
    {
        std::uint32_t slotCount{0};        ///< Number of slots.
        std::uint32_t planeCount{0};       ///< Number of planes in a grain.
        std::uint64_t grainPayloadSize{0}; ///< Size in bytes of the payload of one grain.
        std::vector<int> fds;              ///< One owned descriptor per slot. Empty in a reader until it receives them.
        std::vector<std::uint64_t> sizes;  ///< Size in bytes of each slot's buffer.
    };

    /**
     * Close a set of descriptors.
     * \param[in] fds The descriptors.
     * \param[in] count Their number.
     */
    void closeAll(int const* fds, std::size_t count) noexcept
    {
        for (auto i = std::size_t{0}; i < count; ++i)
        {
            if (fds[i] >= 0)
            {
                ::close(fds[i]);
            }
        }
    }

    /**
     * \param[in] options The backend options as JSON object text.
     * \return true if the options are an empty JSON object.
     */
    bool hasNoOptions(char const* options) noexcept
    {
        try
        {
            auto value = picojson::value{};
            return (options != nullptr) && picojson::parse(value, std::string{options}).empty() && value.is<picojson::object>() &&
                   value.get<picojson::object>().empty();
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * \param[in] flowDefinition The flow definition as JSON text.
     * \return true if its media type is the one the backend stores.
     */
    bool hasStoredMediaType(char const* flowDefinition) noexcept
    {
        try
        {
            auto value = picojson::value{};
            if ((flowDefinition == nullptr) || !picojson::parse(value, std::string{flowDefinition}).empty() || !value.is<picojson::object>())
            {
                return false;
            }
            auto const& root = value.get<picojson::object>();
            auto const it = root.find("media_type");
            return (it != root.end()) && it->second.is<std::string>() && (it->second.get<std::string>() == mxl::tests::memfd::MEDIA_TYPE);
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * Create a memory file of a given size.
     * \param[in] size The size in bytes.
     * \return The descriptor, or -1 on error.
     */
    int createBuffer(std::uint64_t size) noexcept
    {
#ifdef __linux__
        auto const fd = ::memfd_create("mxl-test-memfd", MFD_CLOEXEC);
#else
        char name[] = "/tmp/mxl-test-memfd-XXXXXX";
        auto const fd = ::mkstemp(name);
        if (fd >= 0)
        {
            ::unlink(name);
        }
#endif
        if ((fd >= 0) && (::ftruncate(fd, static_cast<off_t>(size)) != 0))
        {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    /**
     * Create an instance for a flow, after checking the flow and the options.
     * \param[in] info The flow geometry.
     * \param[in] options The backend options.
     * \param[out] out_status Receives the reason when no instance is created.
     * \return The instance, or null.
     */
    Instance* makeInstance(mxlPayloadBackendFlowInfo const* info, char const* options, mxlStatus* out_status) noexcept
    {
        if ((info == nullptr) || (info->structSize < sizeof(mxlPayloadBackendFlowInfo)) || (info->grainCount == 0U) ||
            (info->grainPayloadSize == 0U) || (info->planeCount == 0U) || (info->planeCount > MXL_MAX_PLANES_PER_GRAIN) || !hasNoOptions(options))
        {
            *out_status = MXL_ERR_INVALID_ARG;
            return nullptr;
        }
        if ((info->format != MXL_DATA_FORMAT_VIDEO) || !hasStoredMediaType(info->flowDefinition))
        {
            *out_status = MXL_ERR_UNSUPPORTED_OPERATION;
            return nullptr;
        }

        auto* const instance = new (std::nothrow) Instance{};
        if (instance == nullptr)
        {
            *out_status = MXL_ERR_UNKNOWN;
            return nullptr;
        }
        instance->slotCount = info->grainCount;
        instance->planeCount = info->planeCount;
        instance->grainPayloadSize = info->grainPayloadSize;
        return instance;
    }

    /** \see mxlPayloadBackendApi::prepare */
    mxlStatus prepare(mxlPayloadBackendFlowInfo const* info, char const* options, char const* stagingDir, char const* finalDir,
        mxlPayloadBackendInstance* out_instance) noexcept
    {
        if ((stagingDir == nullptr) || (finalDir == nullptr) || (out_instance == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        auto status = MXL_STATUS_OK;
        auto* const instance = makeInstance(info, options, &status);
        if (instance == nullptr)
        {
            return status;
        }

        try
        {
            for (auto slot = std::uint32_t{0}; slot < instance->slotCount; ++slot)
            {
                auto const fd = createBuffer(instance->grainPayloadSize);
                if (fd < 0)
                {
                    closeAll(instance->fds.data(), instance->fds.size());
                    delete instance;
                    return MXL_ERR_UNKNOWN;
                }
                instance->fds.push_back(fd);
                instance->sizes.push_back(instance->grainPayloadSize);
            }
        }
        catch (...)
        {
            closeAll(instance->fds.data(), instance->fds.size());
            delete instance;
            return MXL_ERR_UNKNOWN;
        }

        *out_instance = reinterpret_cast<mxlPayloadBackendInstance>(instance);
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApi::open */
    mxlStatus open(mxlPayloadBackendFlowInfo const* info, char const* options, char const* flowDir, mxlPayloadBackendInstance* out_instance) noexcept
    {
        if ((flowDir == nullptr) || (out_instance == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        auto status = MXL_STATUS_OK;
        auto* const instance = makeInstance(info, options, &status);
        if (instance == nullptr)
        {
            return status;
        }
        // The descriptors arrive later, through importDescriptors().
        *out_instance = reinterpret_cast<mxlPayloadBackendInstance>(instance);
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApi::destroy */
    void destroy(mxlPayloadBackendInstance handle) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if (instance != nullptr)
        {
            closeAll(instance->fds.data(), instance->fds.size());
            delete instance;
        }
    }

    /** \see mxlPayloadBackendApi::slotStorage */
    mxlStatus slotStorage(mxlPayloadBackendInstance handle, std::uint32_t slot, mxlGrainStorage* out_storage) noexcept
    {
        auto const* const instance = reinterpret_cast<Instance const*>(handle);
        if ((instance == nullptr) || (out_storage == nullptr) || (slot >= instance->slotCount))
        {
            return MXL_ERR_INVALID_ARG;
        }
        if (instance->fds.size() != instance->slotCount)
        {
            return MXL_ERR_INVALID_STATE;
        }

        std::memset(out_storage, 0, sizeof(*out_storage));
        out_storage->version = 1U;
        out_storage->size = sizeof(mxlGrainStorage);
        out_storage->slot = slot;
        mxl::tests::memfd::setSlotStorage(*out_storage, {.fd = instance->fds[slot], .reserved = 0U, .fileSize = instance->sizes[slot]});
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApi::describeLayout */
    mxlStatus describeLayout(mxlPayloadBackendInstance handle, mxlGrainStorageLayout* inout_layout) noexcept
    {
        auto const* const instance = reinterpret_cast<Instance const*>(handle);
        if ((instance == nullptr) || (inout_layout == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        // The planes of a slot follow each other in its buffer, as in the default layout.
        constexpr auto mediaType = std::string_view{mxl::tests::memfd::MEDIA_TYPE};
        auto layout = mxl::tests::memfd::Layout{};
        static_assert(mediaType.size() < sizeof(layout.mediaType));
        std::memcpy(layout.mediaType, mediaType.data(), mediaType.size());
        mxl::tests::memfd::setLayout(*inout_layout, layout);
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApi::mapSlots */
    mxlStatus mapSlots(mxlPayloadBackendInstance handle) noexcept
    {
        auto const* const instance = reinterpret_cast<Instance const*>(handle);
        if (instance == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }
        // Applications map the descriptors themselves. A reader only needs to have received them.
        return (instance->fds.size() == instance->slotCount) ? MXL_STATUS_OK : MXL_ERR_INVALID_STATE;
    }

    /** \see mxlPayloadBackendApi::exportDescriptors */
    mxlStatus exportDescriptors(mxlPayloadBackendInstance handle, int* out_descriptors, std::uint32_t capacity, std::uint32_t* out_count) noexcept
    {
        auto const* const instance = reinterpret_cast<Instance const*>(handle);
        if ((instance == nullptr) || (out_count == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        *out_count = static_cast<std::uint32_t>(instance->fds.size());
        if (out_descriptors == nullptr)
        {
            return MXL_STATUS_OK;
        }
        if (capacity < instance->fds.size())
        {
            return MXL_ERR_INVALID_ARG;
        }
        std::memcpy(out_descriptors, instance->fds.data(), instance->fds.size() * sizeof(int));
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApi::importDescriptors */
    mxlStatus importDescriptors(mxlPayloadBackendInstance handle, int const* descriptors, std::uint32_t count) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if ((instance == nullptr) || ((descriptors == nullptr) && (count > 0U)))
        {
            closeAll(descriptors, (descriptors == nullptr) ? 0U : count);
            return MXL_ERR_INVALID_ARG;
        }

        // One buffer per slot, each large enough for a grain.
        auto valid = (count == instance->slotCount) && instance->fds.empty();
        auto sizes = std::vector<std::uint64_t>{};
        for (auto i = std::uint32_t{0}; valid && (i < count); ++i)
        {
            auto const size = ::lseek(descriptors[i], 0, SEEK_END);
            valid = (size >= 0) && (static_cast<std::uint64_t>(size) >= instance->grainPayloadSize);
            sizes.push_back(static_cast<std::uint64_t>(size));
        }
        if (!valid)
        {
            closeAll(descriptors, count);
            return MXL_ERR_INVALID_STATE;
        }

        instance->fds.assign(descriptors, descriptors + count);
        instance->sizes = std::move(sizes);
        return MXL_STATUS_OK;
    }

    /** The function table of this backend. */
    constexpr auto API = mxlPayloadBackendApi{
        .structSize = sizeof(mxlPayloadBackendApi),
        .abiVersion = MXL_PAYLOAD_BACKEND_ABI_VERSION,
        .name = "test-memfd",
        .storageType = mxl::tests::memfd::STORAGE_TYPE,
        .prepare = &prepare,
        .open = &open,
        .destroy = &destroy,
        .slotStorage = &slotStorage,
        .describeLayout = &describeLayout,
        .mapSlots = &mapSlots,
        .capabilities = MXL_PAYLOAD_BACKEND_CAP_SHARE_DESCRIPTORS,
        .reserved2 = 0U,
        .exportDescriptors = &exportDescriptors,
        .importDescriptors = &importDescriptors,
    };
}

/** \see mxlGetPayloadBackendApiFn */
extern "C" MXL_EXPORT
mxlStatus mxlGetPayloadBackendApi(std::uint32_t requestedVersion, mxlPayloadBackendApi const** out_api)
{
    if (out_api == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }
    if (requestedVersion != MXL_PAYLOAD_BACKEND_ABI_VERSION)
    {
        return MXL_ERR_UNSUPPORTED_OPERATION;
    }
    *out_api = &API;
    return MXL_STATUS_OK;
}
