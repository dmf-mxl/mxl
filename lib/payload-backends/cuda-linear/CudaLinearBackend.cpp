// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file CudaLinearBackend.cpp
 *
 * Payload backend "cuda-linear": keeps the payload of a discrete flow in linear CUDA device memory and shares it
 * with readers in other processes through CUDA IPC.
 *
 * - The writer makes one cudaMalloc for all slots, with each slot starting at a multiple of 256 bytes, and exports
 *   it once with cudaIpcGetMemHandle. It records the handle, the stride and the device UUID in payload.cuda.json,
 *   next to payload.json, before the flow is published.
 * - A reader in another process imports the allocation once, when the application maps the slots, with
 *   cudaIpcOpenMemHandle. A reader on another GPU of the same machine maps it through peer access.
 * - CUDA IPC cannot open a handle in the process that exported it. A reader in the writer's own process therefore
 *   shares the writer's allocation through a registry inside the process. The allocation is reference counted,
 *   so it stays valid for such a reader after the writer is released.
 *
 * Devices are identified by UUID ("deviceUuid" option), never by index, because CUDA device indices depend on
 * CUDA_DEVICE_ORDER and CUDA_VISIBLE_DEVICES, and can differ between processes.
 *
 * Based on the CudaLinearPayloadAllocator by Thomas True in dmf-mxl/mxl pull request #653, which introduced CUDA
 * IPC export and import of grain payloads, peer access for readers on another GPU, and the registry for readers
 * in the writer's process.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <cuda_runtime_api.h>
#include <picojson/wrapper.h>
#include <mxl/dataformat.h>
#include <mxl/platform.h>
#include "mxl-internal/PayloadBackendAbi.h"

namespace
{
    /** Name of this backend. */
    constexpr auto BACKEND_NAME = "cuda-linear";

    /** Name of the file in the flow directory that describes the exported allocation. */
    constexpr auto DESCRIPTOR_FILE_NAME = "payload.cuda.json";

    /** Version of the descriptor format written by this backend. */
    constexpr auto DESCRIPTOR_VERSION = 1.0;

    /** Export format recorded in the descriptor. */
    constexpr auto EXPORT_CUDA_IPC = "cuda-ipc";

    /** Alignment of slot starts within the allocation. */
    constexpr auto SLOT_ALIGNMENT = std::uint64_t{256};

    /** A CUDA device UUID. */
    using Uuid = std::array<std::uint8_t, 16>;

    /**
     * Parse a device UUID, as printed by nvidia-smi ("GPU-" followed by 8-4-4-4-12 hexadecimal digits), or as 32
     * hexadecimal digits with or without hyphens.
     * \param[in] text The UUID text.
     * \param[out] out_uuid Receives the UUID on success.
     * \return true if text is a valid UUID.
     */
    bool parseUuid(std::string const& text, Uuid& out_uuid) noexcept
    {
        auto digits = std::string_view{text};
        if (digits.starts_with("GPU-"))
        {
            digits.remove_prefix(4);
        }

        auto const hexValue = [](char c) -> int
        {
            if ((c >= '0') && (c <= '9'))
            {
                return c - '0';
            }
            if ((c >= 'a') && (c <= 'f'))
            {
                return c - 'a' + 10;
            }
            if ((c >= 'A') && (c <= 'F'))
            {
                return c - 'A' + 10;
            }
            return -1;
        };

        auto count = std::size_t{0};
        auto result = Uuid{};
        for (auto const c : digits)
        {
            if (c == '-')
            {
                continue;
            }
            auto const value = hexValue(c);
            if ((value < 0) || (count >= (2U * result.size())))
            {
                return false;
            }
            result[count / 2U] = static_cast<std::uint8_t>((result[count / 2U] << 4U) | static_cast<unsigned>(value));
            ++count;
        }
        if (count != (2U * result.size()))
        {
            return false;
        }
        out_uuid = result;
        return true;
    }

    /**
     * \param[in] uuid A device UUID.
     * \return The UUID in the format nvidia-smi prints.
     */
    std::string formatUuid(Uuid const& uuid)
    {
        constexpr auto HEX = "0123456789abcdef";
        auto result = std::string{"GPU-"};
        for (auto i = std::size_t{0}; i < uuid.size(); ++i)
        {
            if ((i == 4U) || (i == 6U) || (i == 8U) || (i == 10U))
            {
                result.push_back('-');
            }
            result.push_back(HEX[uuid[i] >> 4U]);
            result.push_back(HEX[uuid[i] & 0x0FU]);
        }
        return result;
    }

    /**
     * Find the index, in this process, of the CUDA device with a given UUID.
     * \param[in] uuid The device UUID.
     * \param[out] out_ordinal Receives the device index on success.
     * \return MXL_STATUS_OK, MXL_ERR_NOT_FOUND if no visible device has that UUID, or MXL_ERR_UNSUPPORTED_OPERATION if
     *      the CUDA runtime or driver is not usable in this process.
     */
    mxlStatus findDevice(Uuid const& uuid, int& out_ordinal) noexcept
    {
        auto count = 0;
        if (::cudaGetDeviceCount(&count) != cudaSuccess)
        {
            (void)::cudaGetLastError();
            return MXL_ERR_UNSUPPORTED_OPERATION;
        }
        for (auto ordinal = 0; ordinal < count; ++ordinal)
        {
            auto properties = cudaDeviceProp{};
            if ((::cudaGetDeviceProperties(&properties, ordinal) == cudaSuccess) &&
                (std::memcmp(properties.uuid.bytes, uuid.data(), uuid.size()) == 0))
            {
                out_ordinal = ordinal;
                return MXL_STATUS_OK;
            }
        }
        return MXL_ERR_NOT_FOUND;
    }

    /**
     * Map a CUDA error to an MXL status, and clear it from the runtime.
     * \param[in] error The CUDA error.
     * \return The MXL status.
     */
    mxlStatus toStatus(cudaError_t error) noexcept
    {
        (void)::cudaGetLastError();
        switch (error)
        {
            case cudaSuccess:                 return MXL_STATUS_OK;
            case cudaErrorNoDevice:           [[fallthrough]];
            case cudaErrorInsufficientDriver: [[fallthrough]];
            case cudaErrorNotSupported:       return MXL_ERR_UNSUPPORTED_OPERATION;
            case cudaErrorInvalidValue:       return MXL_ERR_INVALID_ARG;
            default:                          return MXL_ERR_UNKNOWN;
        }
    }

    /**
     * Device memory allocated by a writer. Shared by the writer and by readers in the same process, and freed when
     * the last of them releases it.
     */
    struct Allocation
    {
        /**
         * \param[in] ordinal The device that holds the memory.
         * \param[in] base The device address returned by cudaMalloc.
         */
        Allocation(int ordinal, void* base) noexcept
            : ordinal{ordinal}
            , base{base}
        {}

        /** Free the memory on its device. */
        ~Allocation()
        {
            if (base != nullptr)
            {
                (void)::cudaSetDevice(ordinal);
                (void)::cudaFree(base);
            }
        }

        Allocation(Allocation const&) = delete;
        Allocation(Allocation&&) = delete;
        Allocation& operator=(Allocation const&) = delete;
        Allocation& operator=(Allocation&&) = delete;

        int ordinal; ///< The device index in this process.
        void* base;  ///< The device address of the first slot.
    };

    /**
     * Allocations of the writers in this process, by flow directory, so that readers in the same process can share
     * them. Holds weak references, so it never keeps memory alive.
     */
    class LocalRegistry
    {
    public:
        /** \return The registry of this process. */
        static LocalRegistry& instance()
        {
            static auto registry = LocalRegistry{};
            return registry;
        }

        /**
         * \param[in] key The canonical flow directory.
         * \param[in] allocation The writer's allocation.
         */
        void publish(std::string const& key, std::shared_ptr<Allocation> const& allocation)
        {
            auto const lock = std::lock_guard{_mutex};
            _entries[key] = allocation;
        }

        /**
         * \param[in] key The canonical flow directory.
         * \return The writer's allocation if it is still alive, otherwise null.
         */
        std::shared_ptr<Allocation> lookup(std::string const& key)
        {
            auto const lock = std::lock_guard{_mutex};
            auto const it = _entries.find(key);
            return (it != _entries.end()) ? it->second.lock() : nullptr;
        }

    private:
        /** Protects _entries. */
        std::mutex _mutex;
        /** Allocations by canonical flow directory. */
        std::unordered_map<std::string, std::weak_ptr<Allocation>> _entries;
    };

    /**
     * \param[in] dir A flow directory, which may not exist yet.
     * \return The key of the flow in the local registry.
     */
    std::string registryKey(std::filesystem::path const& dir)
    {
        return std::filesystem::weakly_canonical(dir).string();
    }

    /**
     * State of one backend instance.
     */
    struct Instance
    {
        std::string registryKey;                     ///< Canonical final flow directory.
        std::filesystem::path descriptorPath;        ///< Path of payload.cuda.json in the published flow.
        std::uint32_t slotCount{0};                  ///< Number of slots.
        std::uint64_t grainPayloadSize{0};           ///< Payload size of one grain.
        std::uint64_t stride{0};                     ///< Distance between slot starts.
        Uuid ownerUuid{};                            ///< Device that holds the memory.
        std::optional<Uuid> requestedUuid;           ///< Device on which a reader wants to access the memory.
        cudaIpcMemHandle_t handle{};                 ///< The exported allocation.
        int localOrdinal{-1};                        ///< Device index in this process on which the memory is accessed.
        std::shared_ptr<Allocation> allocation;      ///< The writer's allocation, in the writer and in readers of the same process.
        void* importedBase{nullptr};                 ///< Address of the imported allocation, in readers of other processes.
        std::once_flag descriptorOnce;               ///< Reads the descriptor once, in readers.
        mxlStatus descriptorStatus{MXL_ERR_UNKNOWN}; ///< Result of reading the descriptor.
        std::once_flag mapOnce;                      ///< Imports the allocation once, in readers.
        mxlStatus mapStatus{MXL_ERR_UNKNOWN};        ///< Result of the import.

        /** \return The device address of the first slot, or null if the allocation is not mapped. */
        [[nodiscard]]
        std::uint8_t* base() const noexcept
        {
            return static_cast<std::uint8_t*>((allocation != nullptr) ? allocation->base : importedBase);
        }
    };

    /**
     * Parse backend options.
     * \param[in] options The options as JSON object text.
     * \param[out] out_uuid Receives the value of "deviceUuid", if present.
     * \return MXL_STATUS_OK, or MXL_ERR_INVALID_ARG if the options are not an object with at most a valid "deviceUuid".
     */
    mxlStatus parseOptions(char const* options, std::optional<Uuid>& out_uuid) noexcept
    {
        try
        {
            auto value = picojson::value{};
            if ((options == nullptr) || !picojson::parse(value, std::string{options}).empty() || !value.is<picojson::object>())
            {
                return MXL_ERR_INVALID_ARG;
            }
            for (auto const& [key, option] : value.get<picojson::object>())
            {
                auto uuid = Uuid{};
                if ((key != "deviceUuid") || !option.is<std::string>() || !parseUuid(option.get<std::string>(), uuid))
                {
                    return MXL_ERR_INVALID_ARG;
                }
                out_uuid = uuid;
            }
            return MXL_STATUS_OK;
        }
        catch (...)
        {
            return MXL_ERR_INVALID_ARG;
        }
    }

    /**
     * \param[in] info The flow geometry passed by libmxl.
     * \return true if the geometry can be served by this backend.
     */
    bool isValidFlowInfo(mxlPayloadBackendFlowInfo const* info) noexcept
    {
        return (info != nullptr) && (info->structSize >= sizeof(mxlPayloadBackendFlowInfo)) && (info->grainCount > 0U) &&
               (info->grainPayloadSize > 0U) && (info->planeCount > 0U) && (info->planeCount <= MXL_MAX_PLANES_PER_GRAIN);
    }

    /**
     * Write the descriptor of a writer's allocation.
     * \param[in] instance The writer's instance.
     * \param[in] path Where to write the descriptor.
     * \return MXL_STATUS_OK, or MXL_ERR_UNKNOWN if the file cannot be written.
     */
    mxlStatus writeDescriptor(Instance const& instance, std::filesystem::path const& path) noexcept
    {
        try
        {
            constexpr auto HEX = "0123456789abcdef";
            auto handleText = std::string{};
            for (auto const c : instance.handle.reserved)
            {
                auto const byte = static_cast<std::uint8_t>(c);
                handleText.push_back(HEX[byte >> 4U]);
                handleText.push_back(HEX[byte & 0x0FU]);
            }

            auto root = picojson::object{};
            root["version"] = picojson::value{DESCRIPTOR_VERSION};
            root["export"] = picojson::value{std::string{EXPORT_CUDA_IPC}};
            root["deviceUuid"] = picojson::value{formatUuid(instance.ownerUuid)};
            root["slotCount"] = picojson::value{static_cast<double>(instance.slotCount)};
            root["stride"] = picojson::value{static_cast<double>(instance.stride)};
            root["handle"] = picojson::value{handleText};

            auto out = std::ofstream{path, std::ios::out | std::ios::trunc};
            out << picojson::value{root}.serialize(true);
            return out ? MXL_STATUS_OK : MXL_ERR_UNKNOWN;
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
    }

    /**
     * Read the descriptor of the writer's allocation into a reader's instance.
     * \param[in,out] instance The reader's instance.
     * \return MXL_STATUS_OK, MXL_ERR_NOT_FOUND if the descriptor is missing, MXL_ERR_UNSUPPORTED_OPERATION if it has a
     *     version this backend does not know, or MXL_ERR_INVALID_STATE if it is invalid.
     */
    mxlStatus readDescriptor(Instance& instance) noexcept
    {
        try
        {
            auto in = std::ifstream{instance.descriptorPath, std::ios::in};
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
            auto const text = [&](char const* key) -> std::string
            {
                auto const it = root.find(key);
                return ((it != root.end()) && it->second.is<std::string>()) ? it->second.get<std::string>() : std::string{};
            };
            auto const number = [&](char const* key) -> double
            {
                auto const it = root.find(key);
                return ((it != root.end()) && it->second.is<double>()) ? it->second.get<double>() : -1.0;
            };

            // The writer can run another build of this backend, for example in another container.
            if (number("version") != DESCRIPTOR_VERSION)
            {
                return MXL_ERR_UNSUPPORTED_OPERATION;
            }

            auto const handleText = text("handle");
            if ((text("export") != EXPORT_CUDA_IPC) || !parseUuid(text("deviceUuid"), instance.ownerUuid) ||
                (number("slotCount") != static_cast<double>(instance.slotCount)) ||
                (number("stride") < static_cast<double>(instance.grainPayloadSize)) || (handleText.size() != (2U * sizeof(instance.handle.reserved))))
            {
                return MXL_ERR_INVALID_STATE;
            }
            instance.stride = static_cast<std::uint64_t>(number("stride"));

            for (auto i = std::size_t{0}; i < sizeof(instance.handle.reserved); ++i)
            {
                auto const byte = std::stoul(handleText.substr(2U * i, 2U), nullptr, 16);
                instance.handle.reserved[i] = static_cast<char>(byte);
            }
            return MXL_STATUS_OK;
        }
        catch (...)
        {
            return MXL_ERR_INVALID_STATE;
        }
    }

    /**
     * Read the descriptor once, and find the device on which this reader accesses the memory, without importing.
     * \param[in,out] instance The reader's instance.
     * \return The result of reading the descriptor.
     */
    mxlStatus ensureDescriptor(Instance& instance) noexcept
    {
        try
        {
            std::call_once(instance.descriptorOnce,
                [&instance]()
                {
                    instance.descriptorStatus = readDescriptor(instance);
                    if (instance.descriptorStatus == MXL_STATUS_OK)
                    {
                        // Best effort: without a usable device the index stays -1 until mapping reports why.
                        auto ordinal = -1;
                        if (findDevice(instance.requestedUuid.value_or(instance.ownerUuid), ordinal) == MXL_STATUS_OK)
                        {
                            instance.localOrdinal = ordinal;
                        }
                    }
                });
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
        return instance.descriptorStatus;
    }

    /**
     * Import the writer's allocation into a reader.
     * \param[in,out] instance The reader's instance.
     * \return MXL_STATUS_OK, or an error code.
     */
    mxlStatus importAllocation(Instance& instance) noexcept
    {
        if (auto const status = ensureDescriptor(instance); status != MXL_STATUS_OK)
        {
            return status;
        }

        try
        {
            // A reader in the writer's process shares the writer's allocation.
            if (auto allocation = LocalRegistry::instance().lookup(instance.registryKey); allocation != nullptr)
            {
                instance.localOrdinal = allocation->ordinal;
                instance.allocation = std::move(allocation);
                return MXL_STATUS_OK;
            }
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }

        auto ownerOrdinal = -1;
        if (auto const status = findDevice(instance.ownerUuid, ownerOrdinal); status != MXL_STATUS_OK)
        {
            // The device that holds the memory is not visible in this process.
            return (status == MXL_ERR_NOT_FOUND) ? MXL_ERR_UNSUPPORTED_OPERATION : status;
        }

        auto localOrdinal = ownerOrdinal;
        if (instance.requestedUuid.has_value())
        {
            if (auto const status = findDevice(*instance.requestedUuid, localOrdinal); status != MXL_STATUS_OK)
            {
                return (status == MXL_ERR_NOT_FOUND) ? MXL_ERR_INVALID_ARG : status;
            }
        }

        if (localOrdinal != ownerOrdinal)
        {
            auto canAccess = 0;
            if ((::cudaDeviceCanAccessPeer(&canAccess, localOrdinal, ownerOrdinal) != cudaSuccess) || (canAccess == 0))
            {
                (void)::cudaGetLastError();
                return MXL_ERR_UNSUPPORTED_OPERATION;
            }
        }

        if (auto const error = ::cudaSetDevice(localOrdinal); error != cudaSuccess)
        {
            return toStatus(error);
        }
        void* base = nullptr;
        if (auto const error = ::cudaIpcOpenMemHandle(&base, instance.handle, cudaIpcMemLazyEnablePeerAccess); error != cudaSuccess)
        {
            return toStatus(error);
        }
        instance.importedBase = base;
        instance.localOrdinal = localOrdinal;
        return MXL_STATUS_OK;
    }

    /**
     * Import the allocation once, if it is not mapped yet.
     * \param[in,out] instance The instance.
     * \return MXL_STATUS_OK, or the error from the import.
     */
    mxlStatus ensureMapped(Instance& instance) noexcept
    {
        try
        {
            std::call_once(instance.mapOnce, [&instance]() { instance.mapStatus = importAllocation(instance); });
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
        return instance.mapStatus;
    }

    /** \see mxlPayloadBackendApiV1::prepare */
    mxlStatus prepare(mxlPayloadBackendFlowInfo const* info, char const* options, char const* stagingDir, char const* finalDir,
        mxlPayloadBackendInstance* out_instance) noexcept
    {
        auto requestedUuid = std::optional<Uuid>{};
        if (!isValidFlowInfo(info) || (stagingDir == nullptr) || (finalDir == nullptr) || (out_instance == nullptr) ||
            (parseOptions(options, requestedUuid) != MXL_STATUS_OK) || !requestedUuid.has_value())
        {
            // A writer must name the device that holds the payload.
            return MXL_ERR_INVALID_ARG;
        }
        if (info->format != MXL_DATA_FORMAT_VIDEO)
        {
            // libmxl leaves the choice of formats to the backend. This one holds video only.
            return MXL_ERR_UNSUPPORTED_OPERATION;
        }

        auto ordinal = -1;
        if (auto const status = findDevice(*requestedUuid, ordinal); status != MXL_STATUS_OK)
        {
            return status;
        }

        auto instance = std::unique_ptr<Instance>{new (std::nothrow) Instance{}};
        if (instance == nullptr)
        {
            return MXL_ERR_UNKNOWN;
        }

        try
        {
            instance->registryKey = registryKey(finalDir);
            instance->descriptorPath = std::filesystem::path{finalDir} / DESCRIPTOR_FILE_NAME;
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
        instance->slotCount = info->grainCount;
        instance->grainPayloadSize = info->grainPayloadSize;
        instance->stride = ((info->grainPayloadSize + SLOT_ALIGNMENT - 1U) / SLOT_ALIGNMENT) * SLOT_ALIGNMENT;
        instance->ownerUuid = *requestedUuid;
        instance->localOrdinal = ordinal;

        if (auto const error = ::cudaSetDevice(ordinal); error != cudaSuccess)
        {
            return toStatus(error);
        }
        auto const size = static_cast<std::size_t>(instance->stride * instance->slotCount);
        void* base = nullptr;
        if (auto const error = ::cudaMalloc(&base, size); error != cudaSuccess)
        {
            return toStatus(error);
        }

        try
        {
            instance->allocation = std::make_shared<Allocation>(ordinal, base);
        }
        catch (...)
        {
            (void)::cudaFree(base);
            return MXL_ERR_UNKNOWN;
        }

        // Start from a defined state, so that readers of grains that were never written see zeros.
        if (auto const error = ::cudaMemset(base, 0, size); error != cudaSuccess)
        {
            return toStatus(error);
        }
        if (auto const error = ::cudaIpcGetMemHandle(&instance->handle, base); error != cudaSuccess)
        {
            return toStatus(error);
        }
        if (auto const status = writeDescriptor(*instance, std::filesystem::path{stagingDir} / DESCRIPTOR_FILE_NAME); status != MXL_STATUS_OK)
        {
            return status;
        }

        try
        {
            LocalRegistry::instance().publish(instance->registryKey, instance->allocation);
            std::call_once(instance->descriptorOnce, [&]() { instance->descriptorStatus = MXL_STATUS_OK; });
            std::call_once(instance->mapOnce, [&]() { instance->mapStatus = MXL_STATUS_OK; });
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }

        *out_instance = reinterpret_cast<mxlPayloadBackendInstance>(instance.release());
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApiV1::open */
    mxlStatus open(mxlPayloadBackendFlowInfo const* info, char const* options, char const* flowDir, mxlPayloadBackendInstance* out_instance) noexcept
    {
        auto requestedUuid = std::optional<Uuid>{};
        if (!isValidFlowInfo(info) || (flowDir == nullptr) || (out_instance == nullptr) || (parseOptions(options, requestedUuid) != MXL_STATUS_OK))
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = std::unique_ptr<Instance>{new (std::nothrow) Instance{}};
        if (instance == nullptr)
        {
            return MXL_ERR_UNKNOWN;
        }
        try
        {
            instance->registryKey = registryKey(flowDir);
            instance->descriptorPath = std::filesystem::path{flowDir} / DESCRIPTOR_FILE_NAME;
        }
        catch (...)
        {
            return MXL_ERR_UNKNOWN;
        }
        instance->slotCount = info->grainCount;
        instance->grainPayloadSize = info->grainPayloadSize;
        instance->requestedUuid = requestedUuid;

        // Nothing is imported, and no CUDA call is made, until the slots are mapped or the layout is described.
        *out_instance = reinterpret_cast<mxlPayloadBackendInstance>(instance.release());
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApiV1::destroy */
    void destroy(mxlPayloadBackendInstance handle) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if (instance == nullptr)
        {
            return;
        }
        if (instance->importedBase != nullptr)
        {
            (void)::cudaSetDevice(instance->localOrdinal);
            (void)::cudaIpcCloseMemHandle(instance->importedBase);
        }
        // Dropping the last reference to a writer's allocation frees it.
        delete instance;
    }

    /** \see mxlPayloadBackendApiV1::deviceIndex */
    std::int32_t deviceIndex(mxlPayloadBackendInstance handle) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if (instance == nullptr)
        {
            return -1;
        }
        (void)ensureDescriptor(*instance);
        return instance->localOrdinal;
    }

    /** \see mxlPayloadBackendApiV1::slotStorage */
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
        out_storage->storageType = MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER;
        out_storage->slot = slot;
        out_storage->cuda.pointer = instance->base() + (instance->stride * slot);
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApiV1::describeLayout */
    mxlStatus describeLayout(mxlPayloadBackendInstance handle, mxlGrainStorageLayout* inout_layout) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if ((instance == nullptr) || (inout_layout == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        if (auto const status = ensureDescriptor(*instance); status != MXL_STATUS_OK)
        {
            return status;
        }

        // The planes of a slot follow each other without padding, as in host memory, so the default planes apply.
        inout_layout->storageType = MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER;
        inout_layout->deviceIndex = instance->localOrdinal;
        std::memcpy(inout_layout->deviceUuid, instance->ownerUuid.data(), instance->ownerUuid.size());
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApiV1::mapSlots */
    mxlStatus mapSlots(mxlPayloadBackendInstance handle) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if (instance == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }
        return ensureMapped(*instance);
    }

    /** The function table of this backend. */
    constexpr auto API = mxlPayloadBackendApiV1{
        .structSize = sizeof(mxlPayloadBackendApiV1),
        .abiVersion = MXL_PAYLOAD_BACKEND_ABI_VERSION,
        .name = BACKEND_NAME,
        .storageType = MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER,
        .reserved = 0U,
        .prepare = &prepare,
        .open = &open,
        .destroy = &destroy,
        .deviceIndex = &deviceIndex,
        .slotStorage = &slotStorage,
        .describeLayout = &describeLayout,
        .mapSlots = &mapSlots,
    };
}

/** \see mxlGetPayloadBackendApiFn */
extern "C" MXL_EXPORT
mxlStatus mxlGetPayloadBackendApi(std::uint32_t requestedVersion, mxlPayloadBackendApiV1 const** out_api)
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
