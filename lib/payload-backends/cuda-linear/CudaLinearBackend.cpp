// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file CudaLinearBackend.cpp
 *
 * Payload backend "cuda-linear": keeps the payload of a discrete flow in linear CUDA device memory and shares it
 * with readers in other processes through a POSIX file descriptor.
 *
 * - The writer creates one virtual-memory allocation for all slots, with each slot starting at a multiple of 256
 *   bytes, and exports it once with cuMemExportToShareableHandle as a POSIX file descriptor. libmxl passes that
 *   descriptor to readers. The descriptor file payload.cuda.json records the stride, the allocation size and the
 *   device UUID. It does not contain the descriptor.
 * - A reader in another process imports the allocation once, when the application maps the slots, with
 *   cuMemImportFromShareableHandle. A reader on another GPU of the same machine maps it through peer access.
 * - A reader in the writer's own process shares the writer's allocation through a registry inside the process.
 *   The allocation is reference counted, so it stays valid for such a reader after the writer is released.
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
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
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
    constexpr auto DESCRIPTOR_VERSION = 2.0;

    /** Export format recorded in the descriptor. The file descriptor itself travels on payload.sock. */
    constexpr auto EXPORT_POSIX_FD = "posix-fd";

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
     * Driver-API entry points for virtual memory, loaded from libcuda.so.1.
     * cudaMalloc memory cannot be exported as a POSIX file descriptor. cuMemCreate memory can.
     */
    class CudaDriver
    {
    public:
        /** Memory mapped into this process, and the writer's exported descriptor when there is one. */
        struct Mapped
        {
            CUdeviceptr base{0};
            std::size_t bytes{0};
            int fd{-1};
        };

        /** \return The driver used by this process. */
        static CudaDriver& instance() noexcept
        {
            static auto driver = CudaDriver{};
            return driver;
        }

        /**
         * Create an allocation on ordinal and export it as a POSIX file descriptor.
         * \param[in] ordinal The device that holds the memory.
         * \param[in] bytes The number of bytes the slots need. Rounded up to the device granularity.
         * \param[out] out_mapped Receives the mapped address, the rounded size and the descriptor.
         * \return MXL_STATUS_OK, or an error code. On error nothing is returned.
         */
        mxlStatus create(int ordinal, std::size_t bytes, Mapped& out_mapped) noexcept
        {
            auto prop = CUmemAllocationProp{};
            if (auto const status = prepare(ordinal, prop); status != MXL_STATUS_OK)
            {
                return status;
            }
            auto granularity = std::size_t{0};
            if (auto const result = _granularity(&granularity, &prop, CU_MEM_ALLOC_GRANULARITY_MINIMUM); result != CUDA_SUCCESS)
            {
                return fromDriver(result);
            }
            if (granularity == 0U)
            {
                return MXL_ERR_UNKNOWN;
            }
            auto const rounded = ((bytes + granularity - 1U) / granularity) * granularity;

            auto handle = CUmemGenericAllocationHandle{};
            if (auto const result = _create(&handle, rounded, &prop, 0); result != CUDA_SUCCESS)
            {
                return fromDriver(result);
            }

            auto mapped = Mapped{};
            if (auto const status = mapHandle(ordinal, handle, rounded, mapped); status != MXL_STATUS_OK)
            {
                (void)_release(handle);
                return status;
            }
            if (auto const result = _export(&mapped.fd, handle, CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR, 0); result != CUDA_SUCCESS)
            {
                unmap(ordinal, mapped.base, mapped.bytes);
                (void)_release(handle);
                return fromDriver(result);
            }
            auto const flags = ::fcntl(mapped.fd, F_GETFD);
            if (flags >= 0)
            {
                (void)::fcntl(mapped.fd, F_SETFD, flags | FD_CLOEXEC);
            }
            (void)_release(handle);
            out_mapped = mapped;
            return MXL_STATUS_OK;
        }

        /**
         * Import an allocation from a POSIX file descriptor and map it on ordinal.
         * \param[in] ordinal The device that will access the memory.
         * \param[in] bytes The allocation size recorded by the writer.
         * \param[in] fd The descriptor. This function closes it.
         * \param[out] out_base Receives the mapped address.
         * \return MXL_STATUS_OK, or an error code.
         */
        mxlStatus import(int ordinal, std::size_t bytes, int fd, CUdeviceptr& out_base) noexcept
        {
            auto prop = CUmemAllocationProp{};
            if (auto const status = prepare(ordinal, prop); status != MXL_STATUS_OK)
            {
                ::close(fd);
                return status;
            }
            auto handle = CUmemGenericAllocationHandle{};
            auto const imported = _import(&handle, reinterpret_cast<void*>(static_cast<std::uintptr_t>(fd)), CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR);
            ::close(fd);
            if (imported != CUDA_SUCCESS)
            {
                return fromDriver(imported);
            }
            auto mapped = Mapped{};
            if (auto const status = mapHandle(ordinal, handle, bytes, mapped); status != MXL_STATUS_OK)
            {
                (void)_release(handle);
                return status;
            }
            (void)_release(handle);
            out_base = mapped.base;
            return MXL_STATUS_OK;
        }

        /**
         * Unmap an address reserved by create() or import().
         * \param[in] ordinal The device whose context mapped the address.
         * \param[in] base The address.
         * \param[in] bytes The mapped size.
         */
        void unmap(int ordinal, CUdeviceptr base, std::size_t bytes) noexcept
        {
            if ((base == 0U) || (bytes == 0U) || !_ready)
            {
                return;
            }
            auto prop = CUmemAllocationProp{};
            (void)prepare(ordinal, prop);
            (void)_unmap(base, bytes);
            (void)_addressFree(base, bytes);
        }

    private:
        using InitFn = CUresult (*)(unsigned int);
        using DeviceGetFn = CUresult (*)(CUdevice*, int);
        using AttributeFn = CUresult (*)(int*, CUdevice_attribute, CUdevice);
        using RetainFn = CUresult (*)(CUcontext*, CUdevice);
        using SetCurrentFn = CUresult (*)(CUcontext);
        using GranularityFn = CUresult (*)(std::size_t*, CUmemAllocationProp const*, CUmemAllocationGranularity_flags);
        using CreateFn = CUresult (*)(CUmemGenericAllocationHandle*, std::size_t, CUmemAllocationProp const*, unsigned long long);
        using ReserveFn = CUresult (*)(CUdeviceptr*, std::size_t, std::size_t, CUdeviceptr, unsigned long long);
        using MapFn = CUresult (*)(CUdeviceptr, std::size_t, std::size_t, CUmemGenericAllocationHandle, unsigned long long);
        using SetAccessFn = CUresult (*)(CUdeviceptr, std::size_t, CUmemAccessDesc const*, std::size_t);
        using ExportFn = CUresult (*)(void*, CUmemGenericAllocationHandle, CUmemAllocationHandleType, unsigned long long);
        using ImportFn = CUresult (*)(CUmemGenericAllocationHandle*, void*, CUmemAllocationHandleType);
        using UnmapFn = CUresult (*)(CUdeviceptr, std::size_t);
        using AddressFreeFn = CUresult (*)(CUdeviceptr, std::size_t);
        using ReleaseFn = CUresult (*)(CUmemGenericAllocationHandle);

        /** Load libcuda.so.1 once. \return true when every entry point is present. */
        bool load() noexcept
        {
            if (_ready)
            {
                return true;
            }
            _library = ::dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
            if (_library == nullptr)
            {
                return false;
            }
            _ready = bind("cuInit", _init) && bind("cuDeviceGet", _deviceGet) && bind("cuDeviceGetAttribute", _attribute) &&
                     bind("cuDevicePrimaryCtxRetain", _retain) && bind("cuCtxSetCurrent", _setCurrent) &&
                     bind("cuMemGetAllocationGranularity", _granularity) && bind("cuMemCreate", _create) && bind("cuMemAddressReserve", _reserve) &&
                     bind("cuMemMap", _map) && bind("cuMemSetAccess", _setAccess) && bind("cuMemExportToShareableHandle", _export) &&
                     bind("cuMemImportFromShareableHandle", _import) && bind("cuMemUnmap", _unmap) && bind("cuMemAddressFree", _addressFree) &&
                     bind("cuMemRelease", _release) && (_init(0) == CUDA_SUCCESS);
            return _ready;
        }

        /**
         * Make the device's primary context current, so the CUDA runtime and the driver API share it.
         * \param[in] ordinal The device index in this process.
         * \param[out] out_prop Receives the allocation properties for that device.
         * \return MXL_STATUS_OK, or an error code.
         */
        mxlStatus prepare(int ordinal, CUmemAllocationProp& out_prop) noexcept
        {
            if (!load())
            {
                return MXL_ERR_UNSUPPORTED_OPERATION;
            }
            auto device = CUdevice{};
            if (auto const result = _deviceGet(&device, ordinal); result != CUDA_SUCCESS)
            {
                return fromDriver(result);
            }
            auto supported = 0;
            if (auto const result = _attribute(&supported, CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_SUPPORTED, device);
                result != CUDA_SUCCESS)
            {
                return fromDriver(result);
            }
            if (supported == 0)
            {
                return MXL_ERR_UNSUPPORTED_OPERATION;
            }
            auto context = CUcontext{};
            if (( _retain(&context, device) != CUDA_SUCCESS) || (_setCurrent(context) != CUDA_SUCCESS))
            {
                return MXL_ERR_UNKNOWN;
            }
            if (auto const error = ::cudaSetDevice(ordinal); error != cudaSuccess)
            {
                return toStatus(error);
            }
            out_prop = CUmemAllocationProp{};
            out_prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
            out_prop.requestedHandleTypes = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR;
            out_prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
            out_prop.location.id = ordinal;
            return MXL_STATUS_OK;
        }

        /**
         * Reserve a virtual address, map handle there, and grant this device read and write access.
         * \param[in] ordinal The device that receives access.
         * \param[in] handle The allocation.
         * \param[in] bytes The size, already a multiple of the granularity.
         * \param[out] out_mapped Receives the address and size. fd stays -1.
         * \return MXL_STATUS_OK, or an error code. On error the handle is still owned by the caller.
         */
        mxlStatus mapHandle(int ordinal, CUmemGenericAllocationHandle handle, std::size_t bytes, Mapped& out_mapped) noexcept
        {
            auto address = CUdeviceptr{0};
            if (auto const result = _reserve(&address, bytes, 0, 0, 0); result != CUDA_SUCCESS)
            {
                return fromDriver(result);
            }
            if (auto const result = _map(address, bytes, 0, handle, 0); result != CUDA_SUCCESS)
            {
                (void)_addressFree(address, bytes);
                return fromDriver(result);
            }
            auto access = CUmemAccessDesc{};
            access.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
            access.location.id = ordinal;
            access.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
            if (auto const result = _setAccess(address, bytes, &access, 1); result != CUDA_SUCCESS)
            {
                (void)_unmap(address, bytes);
                (void)_addressFree(address, bytes);
                return fromDriver(result);
            }
            out_mapped.base = address;
            out_mapped.bytes = bytes;
            return MXL_STATUS_OK;
        }

        /** \return The MXL status for a driver error. */
        static mxlStatus fromDriver(CUresult result) noexcept
        {
            switch (result)
            {
                case CUDA_SUCCESS:               return MXL_STATUS_OK;
                case CUDA_ERROR_NOT_SUPPORTED:   return MXL_ERR_UNSUPPORTED_OPERATION;
                case CUDA_ERROR_INVALID_VALUE:   [[fallthrough]];
                case CUDA_ERROR_INVALID_DEVICE:  return MXL_ERR_INVALID_ARG;
                default:                         return MXL_ERR_UNKNOWN;
            }
        }

        /**
         * \param[in] name The driver entry point.
         * \param[out] out_fn Receives the function.
         * \return true if the entry point exists.
         */
        template <typename Fn>
        bool bind(char const* name, Fn& out_fn) noexcept
        {
            out_fn = reinterpret_cast<Fn>(::dlsym(_library, name));
            return out_fn != nullptr;
        }

        void* _library{nullptr};
        bool _ready{false};
        InitFn _init{nullptr};
        DeviceGetFn _deviceGet{nullptr};
        AttributeFn _attribute{nullptr};
        RetainFn _retain{nullptr};
        SetCurrentFn _setCurrent{nullptr};
        GranularityFn _granularity{nullptr};
        CreateFn _create{nullptr};
        ReserveFn _reserve{nullptr};
        MapFn _map{nullptr};
        SetAccessFn _setAccess{nullptr};
        ExportFn _export{nullptr};
        ImportFn _import{nullptr};
        UnmapFn _unmap{nullptr};
        AddressFreeFn _addressFree{nullptr};
        ReleaseFn _release{nullptr};
    };

    /**
     * Device memory allocated by a writer. Shared by the writer and by readers in the same process, and freed when
     * the last of them releases it.
     */
    struct Allocation
    {
        /**
         * \param[in] ordinal The device whose context mapped the memory.
         * \param[in] base The device address.
         * \param[in] bytes The mapped size.
         */
        Allocation(int ordinal, CUdeviceptr base, std::size_t bytes) noexcept
            : ordinal{ordinal}
            , base{base}
            , bytes{bytes}
        {}

        /** Unmap the memory. */
        ~Allocation()
        {
            CudaDriver::instance().unmap(ordinal, base, bytes);
        }

        Allocation(Allocation const&) = delete;
        Allocation(Allocation&&) = delete;
        Allocation& operator=(Allocation const&) = delete;
        Allocation& operator=(Allocation&&) = delete;

        int ordinal;       ///< The device index in this process.
        CUdeviceptr base;  ///< The device address of the first slot.
        std::size_t bytes; ///< The mapped size.
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
        std::uint64_t allocSize{0};                  ///< Mapped size, a multiple of the device granularity.
        Uuid ownerUuid{};                            ///< Device that holds the memory.
        std::optional<Uuid> requestedUuid;           ///< Device on which a reader wants to access the memory.
        int exportFd{-1};                            ///< The writer's exported descriptor. -1 in a reader.
        int importedFd{-1};                          ///< A descriptor received from libmxl, until it is imported.
        int localOrdinal{-1};                        ///< Device index in this process on which the memory is accessed.
        std::shared_ptr<Allocation> allocation;      ///< The mapped allocation.

        /** Close descriptors this instance still owns. */
        ~Instance()
        {
            if (exportFd >= 0)
            {
                ::close(exportFd);
            }
            if (importedFd >= 0)
            {
                ::close(importedFd);
            }
        }
        std::once_flag descriptorOnce;               ///< Reads the descriptor once, in readers.
        mxlStatus descriptorStatus{MXL_ERR_UNKNOWN}; ///< Result of reading the descriptor.
        std::once_flag mapOnce;                      ///< Imports the allocation once, in readers.
        mxlStatus mapStatus{MXL_ERR_UNKNOWN};        ///< Result of the import.

        /** \return The device address of the first slot, or null if the allocation is not mapped. */
        [[nodiscard]]
        std::uint8_t* base() const noexcept
        {
            return reinterpret_cast<std::uint8_t*>((allocation != nullptr) ? allocation->base : 0U);
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
            auto root = picojson::object{};
            root["version"] = picojson::value{DESCRIPTOR_VERSION};
            root["export"] = picojson::value{std::string{EXPORT_POSIX_FD}};
            root["deviceUuid"] = picojson::value{formatUuid(instance.ownerUuid)};
            root["slotCount"] = picojson::value{static_cast<double>(instance.slotCount)};
            root["stride"] = picojson::value{static_cast<double>(instance.stride)};
            root["allocSize"] = picojson::value{static_cast<double>(instance.allocSize)};

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

            auto const stride = number("stride");
            auto const allocSize = number("allocSize");
            if ((text("export") != EXPORT_POSIX_FD) || !parseUuid(text("deviceUuid"), instance.ownerUuid) ||
                (number("slotCount") != static_cast<double>(instance.slotCount)) || (stride < static_cast<double>(instance.grainPayloadSize)) ||
                (allocSize < (stride * static_cast<double>(instance.slotCount))))
            {
                return MXL_ERR_INVALID_STATE;
            }
            instance.stride = static_cast<std::uint64_t>(stride);
            instance.allocSize = static_cast<std::uint64_t>(allocSize);
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
                if (instance.importedFd >= 0)
                {
                    ::close(instance.importedFd);
                    instance.importedFd = -1;
                }
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

        if (instance.importedFd < 0)
        {
            return MXL_ERR_INVALID_STATE;
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
        if (localOrdinal != ownerOrdinal)
        {
            auto const peer = ::cudaDeviceEnablePeerAccess(ownerOrdinal, 0);
            if ((peer != cudaSuccess) && (peer != cudaErrorPeerAccessAlreadyEnabled))
            {
                return toStatus(peer);
            }
            (void)::cudaGetLastError();
        }

        auto const fd = instance.importedFd;
        instance.importedFd = -1;
        auto base = CUdeviceptr{0};
        if (auto const status = CudaDriver::instance().import(localOrdinal, static_cast<std::size_t>(instance.allocSize), fd, base);
            status != MXL_STATUS_OK)
        {
            return status;
        }
        try
        {
            instance.allocation = std::make_shared<Allocation>(localOrdinal, base, static_cast<std::size_t>(instance.allocSize));
        }
        catch (...)
        {
            CudaDriver::instance().unmap(localOrdinal, base, static_cast<std::size_t>(instance.allocSize));
            return MXL_ERR_UNKNOWN;
        }
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

        auto mapped = CudaDriver::Mapped{};
        auto const bytes = static_cast<std::size_t>(instance->stride * instance->slotCount);
        if (auto const status = CudaDriver::instance().create(ordinal, bytes, mapped); status != MXL_STATUS_OK)
        {
            return status;
        }
        instance->exportFd = mapped.fd;
        instance->allocSize = mapped.bytes;

        try
        {
            instance->allocation = std::make_shared<Allocation>(ordinal, mapped.base, mapped.bytes);
        }
        catch (...)
        {
            CudaDriver::instance().unmap(ordinal, mapped.base, mapped.bytes);
            return MXL_ERR_UNKNOWN;
        }

        // Start from a defined state, so that readers of grains that were never written see zeros.
        if (auto const error = ::cudaMemset(reinterpret_cast<void*>(mapped.base), 0, mapped.bytes); error != cudaSuccess)
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
        // Dropping the instance closes its descriptors and releases the allocation.
        delete reinterpret_cast<Instance*>(handle);
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

    /** \see mxlPayloadBackendApiV1::exportDescriptors */
    mxlStatus exportDescriptors(mxlPayloadBackendInstance handle, std::uint32_t* inout_count, int* fds) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        if ((instance == nullptr) || (inout_count == nullptr) || (instance->exportFd < 0))
        {
            return MXL_ERR_INVALID_ARG;
        }
        if (fds == nullptr)
        {
            *inout_count = 1U;
            return MXL_STATUS_OK;
        }
        if (*inout_count < 1U)
        {
            return MXL_ERR_INVALID_ARG;
        }
        auto const copy = ::fcntl(instance->exportFd, F_DUPFD_CLOEXEC, 0);
        if (copy < 0)
        {
            return MXL_ERR_UNKNOWN;
        }
        fds[0] = copy;
        *inout_count = 1U;
        return MXL_STATUS_OK;
    }

    /** \see mxlPayloadBackendApiV1::importDescriptors */
    mxlStatus importDescriptors(mxlPayloadBackendInstance handle, std::uint32_t count, int const* fds) noexcept
    {
        auto* const instance = reinterpret_cast<Instance*>(handle);
        auto const closeAll = [&]()
        {
            if (fds == nullptr)
            {
                return;
            }
            for (auto i = std::uint32_t{0}; i < count; ++i)
            {
                if (fds[i] >= 0)
                {
                    ::close(fds[i]);
                }
            }
        };
        if ((instance == nullptr) || (count != 1U) || (fds == nullptr) || (fds[0] < 0))
        {
            closeAll();
            return MXL_ERR_INVALID_ARG;
        }
        // The writer, and a reader that already mapped, already has the memory.
        if (instance->allocation != nullptr)
        {
            closeAll();
            return MXL_STATUS_OK;
        }
        if (instance->importedFd >= 0)
        {
            ::close(instance->importedFd);
        }
        instance->importedFd = fds[0];
        return MXL_STATUS_OK;
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
        .exportDescriptors = &exportDescriptors,
        .importDescriptors = &importDescriptors,
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
