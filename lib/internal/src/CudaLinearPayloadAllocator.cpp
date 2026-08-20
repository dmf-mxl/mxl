// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/CudaLinearPayloadAllocator.hpp"

#if defined(MXL_HAS_CUDA) && MXL_HAS_CUDA

#   include <fstream>
#   include <mutex>
#   include <stdexcept>
#   include <string>
#   include <unordered_map>
#   include <cuda_runtime_api.h>
#   include <fmt/format.h>
#   include <picojson/picojson.h>
#   include <mxl/mxl.h>
#   include "mxl-internal/Logging.hpp"
#   include "mxl-internal/PathUtils.hpp"

namespace mxl::lib
{
    namespace
    {
        [[noreturn]]
        void throwCuda(char const* what, cudaError_t err)
        {
            throw std::runtime_error{fmt::format("{}: {} ({})", what, cudaGetErrorString(err), static_cast<int>(err))};
        }

        void checkCuda(char const* what, cudaError_t err)
        {
            if (err != cudaSuccess)
            {
                throwCuda(what, err);
            }
        }

        void fillCommon(mxlPayloadView* outView, Grain const* grain, int32_t deviceIndex, std::uint64_t devicePtr) noexcept
        {
            *outView = {};
            outView->kind = MXL_PAYLOAD_KIND_DEVICE_PTR;
            outView->grainSize = grain->header.info.grainSize;
            outView->deviceIndex = deviceIndex;
            outView->reserved = 0;
            outView->u.devicePtr = devicePtr;
        }

        /**
         * CUDA IPC cannot open a handle inside the process that created it. Keep a process-local
         * registry so multiple MXL instances in the same process can share creator-owned pointers.
         */
        class LocalCudaPayloadRegistry
        {
        public:
            static LocalCudaPayloadRegistry& instance()
            {
                static LocalCudaPayloadRegistry registry;
                return registry;
            }

            void publish(std::filesystem::path const& slotPath, void* devicePtr)
            {
                auto key = std::filesystem::weakly_canonical(slotPath).string();
                auto lock = std::lock_guard{_mutex};
                _entries[key] = devicePtr;
            }

            void* lookup(std::filesystem::path const& slotPath) const
            {
                auto key = std::filesystem::weakly_canonical(slotPath).string();
                auto lock = std::lock_guard{_mutex};
                if (auto it = _entries.find(key); it != _entries.end())
                {
                    return it->second;
                }
                return nullptr;
            }

            void unpublish(std::filesystem::path const& slotPath) noexcept
            {
                try
                {
                    auto key = std::filesystem::weakly_canonical(slotPath).string();
                    auto lock = std::lock_guard{_mutex};
                    _entries.erase(key);
                }
                catch (...)
                {
                }
            }

        private:
            mutable std::mutex _mutex;
            std::unordered_map<std::string, void*> _entries;
        };
    }

    bool isCudaRuntimeAvailable() noexcept
    {
        int deviceCount = 0;
        auto const err = cudaGetDeviceCount(&deviceCount);
        return (err == cudaSuccess) && (deviceCount > 0);
    }

    CudaLinearPayloadAllocator::CudaLinearPayloadAllocator(int32_t deviceIndex)
        : _deviceIndex{deviceIndex}
    {
        if (deviceIndex < 0)
        {
            throw std::invalid_argument{"CudaLinearPayloadAllocator requires deviceIndex >= 0."};
        }
    }

    CudaLinearPayloadAllocator::~CudaLinearPayloadAllocator()
    {
        release();
    }

    mxlPayloadLocation CudaLinearPayloadAllocator::location() const noexcept
    {
        return MXL_PAYLOAD_LOCATION_DEVICE_MEMORY;
    }

    int32_t CudaLinearPayloadAllocator::deviceIndex() const noexcept
    {
        return _deviceIndex;
    }

    char const* CudaLinearPayloadAllocator::backendName() const noexcept
    {
        return PAYLOAD_BACKEND_CUDA_LINEAR;
    }

    std::size_t CudaLinearPayloadAllocator::mappedPayloadBytes() const noexcept
    {
        return 0U;
    }

    void CudaLinearPayloadAllocator::attach(GrainPayloadAttachContext const& context)
    {
        release();

        if (context.grainCount == 0U)
        {
            return;
        }
        if (context.logicalPayloadSize == 0U)
        {
            throw std::invalid_argument{"CudaLinearPayloadAllocator requires a non-zero logical payload size."};
        }

        _logicalPayloadSize = context.logicalPayloadSize;
        checkCuda("cudaSetDevice", cudaSetDevice(_deviceIndex));

        if (context.accessMode == AccessMode::CREATE_READ_WRITE)
        {
            createPayloads(context);
        }
        else
        {
            openPayloads(context);
        }
    }

    mxlStatus CudaLinearPayloadAllocator::viewAt(std::size_t slotIndex, Grain const* grain, mxlPayloadView* outView) const noexcept
    {
        if ((grain == nullptr) || (outView == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }
        if (slotIndex >= _devicePtrs.size())
        {
            return MXL_ERR_INVALID_ARG;
        }

        fillCommon(outView, grain, _deviceIndex, reinterpret_cast<std::uint64_t>(_devicePtrs[slotIndex]));
        return MXL_STATUS_OK;
    }

    void CudaLinearPayloadAllocator::release() noexcept
    {
        if (_devicePtrs.empty())
        {
            return;
        }

        // Best-effort device selection; ignore failures during teardown.
        (void)cudaSetDevice(_deviceIndex);

        for (auto i = std::size_t{0}; i < _devicePtrs.size(); ++i)
        {
            auto* ptr = _devicePtrs[i];
            if (ptr == nullptr)
            {
                continue;
            }
            if (_ownsDeviceMemory)
            {
                if (i < _slotPaths.size())
                {
                    LocalCudaPayloadRegistry::instance().unpublish(_slotPaths[i]);
                }
                (void)cudaFree(ptr);
            }
            else if (!_borrowedFromLocalRegistry)
            {
                (void)cudaIpcCloseMemHandle(ptr);
            }
        }

        _devicePtrs.clear();
        _slotPaths.clear();
        _ownsDeviceMemory = false;
        _borrowedFromLocalRegistry = false;
        _logicalPayloadSize = 0;
    }

    void CudaLinearPayloadAllocator::createPayloads(GrainPayloadAttachContext const& context)
    {
        _ownsDeviceMemory = true;
        _borrowedFromLocalRegistry = false;
        _devicePtrs.resize(context.grainCount, nullptr);
        _slotPaths.resize(context.grainCount);

        try
        {
            for (auto i = std::size_t{0}; i < context.grainCount; ++i)
            {
                void* ptr = nullptr;
                checkCuda("cudaMalloc", cudaMalloc(&ptr, context.logicalPayloadSize));
                _devicePtrs[i] = ptr;
            }

            auto const payloadDir = makePayloadDirectoryName(context.flowDir);
            std::filesystem::create_directories(payloadDir);

            for (auto i = std::size_t{0}; i < context.grainCount; ++i)
            {
                cudaIpcMemHandle_t handle{};
                checkCuda("cudaIpcGetMemHandle", cudaIpcGetMemHandle(&handle, _devicePtrs[i]));

                auto const slotPath = makePayloadSlotFilePath(context.flowDir, static_cast<unsigned>(i));
                _slotPaths[i] = slotPath;
                auto out = std::ofstream{slotPath, std::ios::binary | std::ios::trunc};
                if (!out)
                {
                    throw std::filesystem::filesystem_error{
                        "Failed to write CUDA IPC handle.", slotPath, std::make_error_code(std::errc::io_error)};
                }
                out.write(reinterpret_cast<char const*>(&handle), static_cast<std::streamsize>(sizeof handle));
                if (!out)
                {
                    throw std::filesystem::filesystem_error{
                        "Failed to write CUDA IPC handle.", slotPath, std::make_error_code(std::errc::io_error)};
                }

                LocalCudaPayloadRegistry::instance().publish(slotPath, _devicePtrs[i]);
            }

            writeDescriptor(context);
            MXL_DEBUG("CudaLinearPayloadAllocator created {} device buffers ({} bytes each) on device {}",
                context.grainCount,
                context.logicalPayloadSize,
                _deviceIndex);
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    void CudaLinearPayloadAllocator::openPayloads(GrainPayloadAttachContext const& context)
    {
        _ownsDeviceMemory = false;
        _borrowedFromLocalRegistry = false;
        _devicePtrs.resize(context.grainCount, nullptr);
        _slotPaths.resize(context.grainCount);

        try
        {
            for (auto i = std::size_t{0}; i < context.grainCount; ++i)
            {
                auto const slotPath = makePayloadSlotFilePath(context.flowDir, static_cast<unsigned>(i));
                _slotPaths[i] = slotPath;

                if (auto* localPtr = LocalCudaPayloadRegistry::instance().lookup(slotPath); localPtr != nullptr)
                {
                    // Same process as the creator: reuse the original pointer (CUDA IPC forbids
                    // opening a handle in the creating process).
                    _devicePtrs[i] = localPtr;
                    _borrowedFromLocalRegistry = true;
                    continue;
                }

                auto in = std::ifstream{slotPath, std::ios::binary};
                if (!in)
                {
                    throw std::filesystem::filesystem_error{
                        "Failed to read CUDA IPC handle.", slotPath, std::make_error_code(std::errc::no_such_file_or_directory)};
                }

                cudaIpcMemHandle_t handle{};
                in.read(reinterpret_cast<char*>(&handle), static_cast<std::streamsize>(sizeof handle));
                if (!in || (in.gcount() != static_cast<std::streamsize>(sizeof handle)))
                {
                    throw std::filesystem::filesystem_error{
                        "Truncated CUDA IPC handle.", slotPath, std::make_error_code(std::errc::io_error)};
                }

                void* ptr = nullptr;
                checkCuda("cudaIpcOpenMemHandle", cudaIpcOpenMemHandle(&ptr, handle, cudaIpcMemLazyEnablePeerAccess));
                _devicePtrs[i] = ptr;
            }

            MXL_DEBUG("CudaLinearPayloadAllocator imported {} device buffers on device {} (localRegistry={})",
                context.grainCount,
                _deviceIndex,
                _borrowedFromLocalRegistry);
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    void CudaLinearPayloadAllocator::writeDescriptor(GrainPayloadAttachContext const& context) const
    {
        picojson::object root;
        root["version"] = picojson::value{1.0};
        root["backend"] = picojson::value{std::string{PAYLOAD_BACKEND_CUDA_LINEAR}};
        root["location"] = picojson::value{std::string{"device"}};
        root["deviceIndex"] = picojson::value{static_cast<double>(_deviceIndex)};
        root["grainCount"] = picojson::value{static_cast<double>(context.grainCount)};
        root["grainSize"] = picojson::value{static_cast<double>(context.logicalPayloadSize)};
        root["export"] = picojson::value{std::string{"cuda-ipc"}};

        picojson::array slots;
        slots.reserve(context.grainCount);
        for (auto i = std::size_t{0}; i < context.grainCount; ++i)
        {
            picojson::object slot;
            slot["index"] = picojson::value{static_cast<double>(i)};
            slot["path"] = picojson::value{fmt::format("{}/{}.{}", PAYLOAD_DIRECTORY_NAME, PAYLOAD_SLOT_FILE_NAME_STEM, i)};
            slot["size"] = picojson::value{static_cast<double>(context.logicalPayloadSize)};
            slots.emplace_back(slot);
        }
        root["slots"] = picojson::value{slots};

        auto const path = makePayloadDescriptorFilePath(context.flowDir);
        auto out = std::ofstream{path, std::ios::trunc};
        if (!out)
        {
            throw std::filesystem::filesystem_error{"Failed to write payload.json.", path, std::make_error_code(std::errc::io_error)};
        }
        out << picojson::value{root}.serialize(true);
    }
}

#endif // MXL_HAS_CUDA
