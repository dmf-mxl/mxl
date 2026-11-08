// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file TestFileBackend.cpp
 *
 * Payload backend used by the tests. It keeps the payload of all slots in one file named payload.test in the
 * flow directory and describes the slots as host pointers into a shared mapping of that file. It needs no
 * device, so the complete backend path runs in CI: loading, prepare in the temporary directory, publish by
 * rename, open, lazy import and release.
 *
 * Writers accept one backend option, "alignment": the distance in bytes between the starts of two slots is rounded
 * up to it. It must be a multiple of 256. The backend records the resulting stride in payload.test.json, which readers
 * read when they map the slots. Readers accept no backend options.
 *
 * Compile with MXL_TEST_BACKEND_NAME set to the backend name as a string literal. The tests also build this
 * file with a name that does not match the library name, to check that libmxl rejects it, and with
 * MXL_TEST_BACKEND_CLAIM_GEOMETRY defined, which makes the backend claim to define media types without providing the
 * function that describes their geometry, which libmxl rejects too.
 *
 * MXL_TEST_BACKEND_STORAGE_TYPE sets the storage type the backend reports, MXL_PAYLOAD_STORAGE_HOST_POINTER by
 * default. The tests also build this file with a storage type of its own, which this backend defines as follows: the
 * slot descriptions hold the same host pointer, and the reserved bytes of the layout hold the media type of the flow,
 * read from the flow definition, as a null terminated string.
 */

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <picojson/wrapper.h>
#include <mxl/dataformat.h>
#include <mxl/platform.h>
#include "mxl-internal/PayloadBackendAbi.h"

#ifndef MXL_TEST_BACKEND_NAME
#   error "MXL_TEST_BACKEND_NAME must be defined."
#endif

#ifndef MXL_TEST_BACKEND_STORAGE_TYPE
#   define MXL_TEST_BACKEND_STORAGE_TYPE MXL_PAYLOAD_STORAGE_HOST_POINTER
#endif

namespace
{
    /** The storage type this backend reports. */
    constexpr auto STORAGE_TYPE = MXL_TEST_BACKEND_STORAGE_TYPE;

    /** Whether the storage type is this backend's own, whose layout holds the media type of the flow. */
    constexpr auto OWN_STORAGE_TYPE = std::string_view{STORAGE_TYPE} != MXL_PAYLOAD_STORAGE_HOST_POINTER;

    /** Name of the file that holds the payload in the flow directory. */
    constexpr auto PAYLOAD_FILE_NAME = "/payload.test";

    /** Name of the file in the flow directory that records the slot stride. */
    constexpr auto STRIDE_FILE_NAME = "/payload.test.json";

    /** Version of the format of payload.test.json. A backend refuses files in a format it does not know. */
    constexpr auto STRIDE_FILE_VERSION = 1.0;

    /** Default and minimum alignment of slot starts. */
    constexpr auto SLOT_ALIGNMENT = std::uint64_t{256};

    /**
     * State of one backend instance.
     */
    struct Instance
    {
        /** Path of the payload file. */
        std::string path;
        /** Path of the file that records the slot stride. */
        std::string stridePath;
        /** Number of slots. */
        std::uint32_t slotCount{0};
        /** Distance in bytes between the starts of two slots. */
        std::uint64_t stride{0};
        /** The media type from the flow definition, or empty if it has none. */
        std::string mediaType;
        /** Whether the mapping is writable. */
        bool writable{false};
        /** Makes the first mapping happen once, even with concurrent first accesses. */
        std::once_flag mapOnce;
        /** Result of the mapping, valid once mapOnce has run. */
        mxlStatus mapStatus{MXL_ERR_UNKNOWN};
        /** Start of the mapping, or null. */
        void* base{nullptr};
        /** Size of the mapping in bytes. */
        std::size_t mappedSize{0};
    };

    /**
     * \param[in] payloadSize The payload size of one grain.
     * \param[in] alignment The alignment of slot starts.
     * \return payloadSize rounded up to alignment.
     */
    std::uint64_t strideFor(std::uint64_t payloadSize, std::uint64_t alignment) noexcept
    {
        return ((payloadSize + alignment - 1U) / alignment) * alignment;
    }

    /**
     * Parse the backend options of a writer.
     * \param[in] options The options as JSON object text.
     * \param[out] out_alignment Receives the requested slot alignment, or SLOT_ALIGNMENT if none was requested.
     * \return MXL_STATUS_OK, or MXL_ERR_INVALID_ARG if the options are invalid.
     */
    mxlStatus parseWriterOptions(char const* options, std::uint64_t& out_alignment) noexcept
    {
        try
        {
            auto value = picojson::value{};
            if (!picojson::parse(value, std::string{options}).empty() || !value.is<picojson::object>())
            {
                return MXL_ERR_INVALID_ARG;
            }

            out_alignment = SLOT_ALIGNMENT;
            for (auto const& [key, option] : value.get<picojson::object>())
            {
                if ((key != "alignment") || !option.is<double>())
                {
                    return MXL_ERR_INVALID_ARG;
                }
                auto const alignment = option.get<double>();
                if ((alignment < static_cast<double>(SLOT_ALIGNMENT)) || (alignment > 1048576.0) ||
                    ((static_cast<std::uint64_t>(alignment) % SLOT_ALIGNMENT) != 0U) ||
                    (alignment != static_cast<double>(static_cast<std::uint64_t>(alignment))))
                {
                    return MXL_ERR_INVALID_ARG;
                }
                out_alignment = static_cast<std::uint64_t>(alignment);
            }
            return MXL_STATUS_OK;
        }
        catch (...)
        {
            return MXL_ERR_INVALID_ARG;
        }
    }

    /**
     * Check the backend options of a reader. The test backend accepts none.
     * \param[in] options The options as JSON object text.
     * \return MXL_STATUS_OK, or MXL_ERR_INVALID_ARG if the options are not an empty object.
     */
    mxlStatus checkReaderOptions(char const* options) noexcept
    {
        try
        {
            auto value = picojson::value{};
            if (!picojson::parse(value, std::string{options}).empty() || !value.is<picojson::object>() || !value.get<picojson::object>().empty())
            {
                return MXL_ERR_INVALID_ARG;
            }
            return MXL_STATUS_OK;
        }
        catch (...)
        {
            return MXL_ERR_INVALID_ARG;
        }
    }

    /**
     * Record the slot stride of an instance for readers.
     * \param[in] instance The instance of the writer.
     * \return MXL_STATUS_OK, or an error code.
     */
    mxlStatus writeStride(Instance const& instance) noexcept
    {
        try
        {
            auto root = picojson::object{};
            root["version"] = picojson::value{STRIDE_FILE_VERSION};
            root["stride"] = picojson::value{static_cast<double>(instance.stride)};
            auto out = std::ofstream{instance.stridePath, std::ios::out | std::ios::trunc};
            out << picojson::value{root}.serialize();
            return out ? MXL_STATUS_OK : MXL_ERR_UNKNOWN;
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
    }

    /**
     * Read the slot stride that the writer recorded.
     * \param[in,out] instance The instance of a reader. Receives the stride.
     * \return MXL_STATUS_OK, MXL_ERR_NOT_FOUND if the file does not exist, MXL_ERR_UNSUPPORTED_OPERATION if it has another
     *     version or none, MXL_ERR_INVALID_STATE if it is invalid, or MXL_ERR_UNKNOWN.
     */
    mxlStatus readStride(Instance& instance) noexcept
    {
        try
        {
            auto in = std::ifstream{instance.stridePath, std::ios::in};
            if (!in)
            {
                return MXL_ERR_NOT_FOUND;
            }
            auto value = picojson::value{};
            if (!picojson::parse(value, in).empty() || !value.is<picojson::object>())
            {
                return MXL_ERR_INVALID_STATE;
            }
            auto const& root = value.get<picojson::object>();
            if (auto const version = root.find("version");
                (version == root.end()) || !version->second.is<double>() || (version->second.get<double>() != STRIDE_FILE_VERSION))
            {
                return MXL_ERR_UNSUPPORTED_OPERATION;
            }
            auto const it = root.find("stride");
            if ((it == root.end()) || !it->second.is<double>() || (it->second.get<double>() < 1.0))
            {
                return MXL_ERR_INVALID_STATE;
            }
            instance.stride = static_cast<std::uint64_t>(it->second.get<double>());
            return MXL_STATUS_OK;
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
    }

    /**
     * Map the payload file of an instance, creating it first if requested.
     * \param[in,out] instance The instance to map.
     * \param[in] create Whether to create and size the file.
     * \return MXL_STATUS_OK, or an error code.
     */
    mxlStatus mapPayloadFile(Instance& instance, bool create) noexcept
    {
        auto const size = static_cast<std::size_t>(instance.stride * instance.slotCount);
        auto const flags = create ? (O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC) : ((instance.writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
        auto const fd = ::open(instance.path.c_str(), flags, 0644);
        if (fd < 0)
        {
            return (errno == ENOENT) ? MXL_ERR_NOT_FOUND : MXL_ERR_UNKNOWN;
        }

        auto status = MXL_STATUS_OK;
        if (create && (::ftruncate(fd, static_cast<off_t>(size)) != 0))
        {
            status = MXL_ERR_UNKNOWN;
        }

        using stat_t = struct ::stat;
        auto st = stat_t{};
        if ((status == MXL_STATUS_OK) && ((::fstat(fd, &st) != 0) || (static_cast<std::size_t>(st.st_size) < size)))
        {
            status = MXL_ERR_INVALID_STATE;
        }

        if (status == MXL_STATUS_OK)
        {
            auto const protection = instance.writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
            auto* const base = ::mmap(nullptr, size, protection, MAP_SHARED, fd, 0);
            if (base == MAP_FAILED)
            {
                status = MXL_ERR_UNKNOWN;
            }
            else
            {
                instance.base = base;
                instance.mappedSize = size;
            }
        }

        ::close(fd);
        return status;
    }

    /**
     * Read the media type of a flow definition.
     * \param[in] flowDefinition The flow definition as JSON text.
     * \return The media_type, or an empty string if the definition has none.
     */
    std::string readMediaType(char const* flowDefinition)
    {
        auto value = picojson::value{};
        if (picojson::parse(value, std::string{flowDefinition}).empty() && value.is<picojson::object>())
        {
            auto const& root = value.get<picojson::object>();
            if (auto const it = root.find("media_type"); (it != root.end()) && it->second.is<std::string>())
            {
                return it->second.get<std::string>();
            }
        }
        return {};
    }

    /**
     * Allocate an instance for a flow.
     * \param[in] info The flow geometry.
     * \param[in] dir The flow directory that holds the payload file.
     * \return The instance, or null if info is invalid or memory is exhausted.
     */
    Instance* makeInstance(mxlPayloadBackendFlowInfo const* info, char const* dir) noexcept
    {
        if ((info == nullptr) || (dir == nullptr) || (info->structSize < sizeof(mxlPayloadBackendFlowInfo)) || (info->grainCount == 0U) ||
            (info->grainPayloadSize == 0U) || (info->planeCount == 0U) || (info->planeCount > MXL_MAX_PLANES_PER_GRAIN) ||
            (info->flowDefinition == nullptr))
        {
            return nullptr;
        }

        auto* const instance = new (std::nothrow) Instance{};
        if (instance != nullptr)
        {
            try
            {
                instance->path = std::string{dir} + PAYLOAD_FILE_NAME;
                instance->stridePath = std::string{dir} + STRIDE_FILE_NAME;
                instance->mediaType = readMediaType(info->flowDefinition);
            }
            catch (...)
            {
                delete instance;
                return nullptr;
            }
            instance->slotCount = info->grainCount;
            instance->stride = strideFor(info->grainPayloadSize, SLOT_ALIGNMENT);
            instance->writable = (info->accessMode == MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE);
        }
        return instance;
    }

    /** \see mxlPayloadBackendApi::prepare */
    mxlStatus prepare(mxlPayloadBackendFlowInfo const* info, char const* options, char const* stagingDir, char const* finalDir,
        mxlPayloadBackendInstance* out_instance) noexcept
    {
        auto alignment = SLOT_ALIGNMENT;
        if ((options == nullptr) || (finalDir == nullptr) || (out_instance == nullptr) || (parseWriterOptions(options, alignment) != MXL_STATUS_OK))
        {
            return MXL_ERR_INVALID_ARG;
        }
        // libmxl leaves the choice of formats to the backend. This one stores the discrete formats.
        if ((info != nullptr) && (info->format != MXL_DATA_FORMAT_VIDEO) && (info->format != MXL_DATA_FORMAT_DATA))
        {
            return MXL_ERR_UNSUPPORTED_OPERATION;
        }

        auto* const instance = makeInstance(info, stagingDir);
        if (instance == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }
        instance->stride = strideFor(info->grainPayloadSize, alignment);

        // The writer allocates at creation time. Only readers map lazily.
        instance->writable = true;
        auto status = mapPayloadFile(*instance, true);
        if (status == MXL_STATUS_OK)
        {
            status = writeStride(*instance);
        }
        if (status != MXL_STATUS_OK)
        {
            ::unlink(instance->path.c_str());
            ::unlink(instance->stridePath.c_str());
            if (instance->base != nullptr)
            {
                ::munmap(instance->base, instance->mappedSize);
            }
            delete instance;
            return status;
        }
        std::call_once(instance->mapOnce, [instance]() { instance->mapStatus = MXL_STATUS_OK; });

        *out_instance = reinterpret_cast<mxlPayloadBackendInstance>(instance);
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApi::open */
    mxlStatus open(mxlPayloadBackendFlowInfo const* info, char const* options, char const* flowDir, mxlPayloadBackendInstance* out_instance) noexcept
    {
        if ((options == nullptr) || (out_instance == nullptr) || (checkReaderOptions(options) != MXL_STATUS_OK))
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto* const instance = makeInstance(info, flowDir);
        if (instance == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }

        // Nothing is mapped here. The first call to mapSlots() or slotStorage() maps the file.
        *out_instance = reinterpret_cast<mxlPayloadBackendInstance>(instance);
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApi::destroy */
    void destroy(mxlPayloadBackendInstance handle) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if (instance != nullptr)
        {
            if (instance->base != nullptr)
            {
                ::munmap(instance->base, instance->mappedSize);
            }
            delete instance;
        }
    }

    /**
     * Map the payload file of an instance if it is not mapped yet.
     * \param[in,out] instance The instance.
     * \return MXL_STATUS_OK, or the error from the first mapping attempt.
     */
    mxlStatus ensureMapped(Instance& instance) noexcept
    {
        try
        {
            std::call_once(instance.mapOnce,
                [&instance]()
                {
                    instance.mapStatus = readStride(instance);
                    if (instance.mapStatus == MXL_STATUS_OK)
                    {
                        instance.mapStatus = mapPayloadFile(instance, false);
                    }
                });
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
        return instance.mapStatus;
    }

    /** \see mxlPayloadBackendApi::mapSlots */
    mxlStatus mapSlots(mxlPayloadBackendInstance handle) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if (instance == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }
        return ensureMapped(*instance);
    }

    /** \see mxlPayloadBackendApi::slotStorage */
    mxlStatus slotStorage(mxlPayloadBackendInstance handle, std::uint32_t slot, mxlGrainStorage* out_storage) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if ((instance == nullptr) || (out_storage == nullptr) || (slot >= instance->slotCount))
        {
            return MXL_ERR_INVALID_ARG;
        }

        if (auto const status = ensureMapped(*instance); status != MXL_STATUS_OK)
        {
            return status;
        }

        std::memset(out_storage, 0, sizeof(*out_storage));
        out_storage->version = 1U;
        out_storage->size = sizeof(*out_storage);
        out_storage->slot = slot;
        out_storage->host.pointer = static_cast<std::uint8_t*>(instance->base) + (instance->stride * slot);
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

        // The planes of a slot are stored like host memory, so the default planes apply.
        if constexpr (OWN_STORAGE_TYPE)
        {
            // The last byte stays zero, so the string is always terminated.
            auto const length = std::min(instance->mediaType.size(), sizeof(inout_layout->reserved) - 1U);
            std::memcpy(inout_layout->reserved, instance->mediaType.data(), length);
        }
        return MXL_STATUS_OK;
    }

    /** The function table of this backend. */
    constexpr auto API = mxlPayloadBackendApi{
        .structSize = sizeof(mxlPayloadBackendApi),
        .abiVersion = MXL_PAYLOAD_BACKEND_ABI_VERSION,
        .name = MXL_TEST_BACKEND_NAME,
        .storageType = STORAGE_TYPE,
        .prepare = &prepare,
        .open = &open,
        .destroy = &destroy,
        .slotStorage = &slotStorage,
        .describeLayout = &describeLayout,
        .mapSlots = &mapSlots,
#ifdef MXL_TEST_BACKEND_CLAIM_GEOMETRY
        .capabilities = MXL_PAYLOAD_BACKEND_CAP_DESCRIBE_GEOMETRY,
#else
        .capabilities = 0U,
#endif
        .reserved2 = 0U,
        .exportDescriptors = nullptr,
        .importDescriptors = nullptr,
        .describeGeometry = nullptr,
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
