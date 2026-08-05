// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/GrainPayloadAllocator.hpp"
#include <fstream>
#include <stdexcept>
#include <string>
#include <picojson/picojson.h>
#include <mxl/mxl.h>
#include "mxl-internal/CudaLinearPayloadAllocator.hpp"
#include "mxl-internal/PathUtils.hpp"

namespace mxl::lib
{
    namespace
    {
        void fillCommon(mxlPayloadView* outView, mxlPayloadKind kind, Grain const* grain, int32_t deviceIndex) noexcept
        {
            *outView = {};
            outView->kind = kind;
            outView->grainSize = grain->header.info.grainSize;
            outView->deviceIndex = deviceIndex;
            outView->reserved = 0;
        }

        std::string normalizeBackend(std::string const& backend, mxlPayloadLocation location)
        {
            if (!backend.empty())
            {
                return backend;
            }
            if (location == MXL_PAYLOAD_LOCATION_HOST_MEMORY)
            {
                return PAYLOAD_BACKEND_HOST;
            }
#if defined(MXL_HAS_CUDA) && MXL_HAS_CUDA
            if (isCudaRuntimeAvailable())
            {
                return PAYLOAD_BACKEND_CUDA_LINEAR;
            }
#endif
            return PAYLOAD_BACKEND_PLACEHOLDER;
        }

        void writePlaceholderDescriptor(GrainPayloadAttachContext const& context, int32_t deviceIndex)
        {
            picojson::object root;
            root["version"] = picojson::value{1.0};
            root["backend"] = picojson::value{std::string{PAYLOAD_BACKEND_PLACEHOLDER}};
            root["location"] = picojson::value{std::string{"device"}};
            root["deviceIndex"] = picojson::value{static_cast<double>(deviceIndex)};
            root["grainCount"] = picojson::value{static_cast<double>(context.grainCount)};
            root["grainSize"] = picojson::value{static_cast<double>(context.logicalPayloadSize)};
            root["export"] = picojson::value{std::string{"none"}};
            root["slots"] = picojson::value{picojson::array{}};

            auto const path = makePayloadDescriptorFilePath(context.flowDir);
            auto out = std::ofstream{path, std::ios::trunc};
            if (!out)
            {
                throw std::filesystem::filesystem_error{"Failed to write payload.json.", path, std::make_error_code(std::errc::io_error)};
            }
            out << picojson::value{root}.serialize(true);
        }
    }

    HostMappedPayloadAllocator::HostMappedPayloadAllocator(std::size_t mappedPayloadBytes) noexcept
        : _mappedPayloadBytes{mappedPayloadBytes}
    {}

    mxlPayloadLocation HostMappedPayloadAllocator::location() const noexcept
    {
        return MXL_PAYLOAD_LOCATION_HOST_MEMORY;
    }

    int32_t HostMappedPayloadAllocator::deviceIndex() const noexcept
    {
        return -1;
    }

    char const* HostMappedPayloadAllocator::backendName() const noexcept
    {
        return PAYLOAD_BACKEND_HOST;
    }

    std::size_t HostMappedPayloadAllocator::mappedPayloadBytes() const noexcept
    {
        return _mappedPayloadBytes;
    }

    void HostMappedPayloadAllocator::attach(GrainPayloadAttachContext const&)
    {
        // Host payloads are embedded in the grain mmap created by DiscreteFlowData::emplaceGrain.
    }

    mxlStatus HostMappedPayloadAllocator::viewAt(std::size_t, Grain const* grain, mxlPayloadView* outView) const noexcept
    {
        if ((grain == nullptr) || (outView == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }

        fillCommon(outView, MXL_PAYLOAD_KIND_HOST_PTR, grain, -1);
        outView->u.hostPtr = reinterpret_cast<std::uint8_t*>(const_cast<GrainHeader*>(&grain->header) + 1);
        return MXL_STATUS_OK;
    }

    DevicePayloadPlaceholderAllocator::DevicePayloadPlaceholderAllocator(int32_t deviceIndex) noexcept
        : _deviceIndex{deviceIndex}
    {}

    mxlPayloadLocation DevicePayloadPlaceholderAllocator::location() const noexcept
    {
        return MXL_PAYLOAD_LOCATION_DEVICE_MEMORY;
    }

    int32_t DevicePayloadPlaceholderAllocator::deviceIndex() const noexcept
    {
        return _deviceIndex;
    }

    char const* DevicePayloadPlaceholderAllocator::backendName() const noexcept
    {
        return PAYLOAD_BACKEND_PLACEHOLDER;
    }

    std::size_t DevicePayloadPlaceholderAllocator::mappedPayloadBytes() const noexcept
    {
        return 0U;
    }

    void DevicePayloadPlaceholderAllocator::attach(GrainPayloadAttachContext const& context)
    {
        if (context.accessMode == AccessMode::CREATE_READ_WRITE)
        {
            writePlaceholderDescriptor(context, _deviceIndex);
        }
    }

    mxlStatus DevicePayloadPlaceholderAllocator::viewAt(std::size_t, Grain const* grain, mxlPayloadView* outView) const noexcept
    {
        if ((grain == nullptr) || (outView == nullptr))
        {
            return MXL_ERR_INVALID_ARG;
        }

        fillCommon(outView, MXL_PAYLOAD_KIND_DEVICE_PTR, grain, _deviceIndex);
        outView->u.devicePtr = 0;
        return MXL_ERR_UNSUPPORTED_OPERATION;
    }

    std::unique_ptr<GrainPayloadAllocator> makeGrainPayloadAllocator(GrainPayloadAllocatorSpec const& spec)
    {
        auto const backend = normalizeBackend(spec.backend, spec.location);

        if (spec.location == MXL_PAYLOAD_LOCATION_HOST_MEMORY)
        {
            if (spec.deviceIndex != -1)
            {
                throw std::invalid_argument{"deviceIndex must be -1 when payloadLocation is host memory."};
            }
            if (!backend.empty() && (backend != PAYLOAD_BACKEND_HOST))
            {
                throw std::invalid_argument{"payload backend \"" + backend + "\" is incompatible with host payload location."};
            }
            return std::make_unique<HostMappedPayloadAllocator>(spec.logicalPayloadSize);
        }

        if (spec.location != MXL_PAYLOAD_LOCATION_DEVICE_MEMORY)
        {
            throw std::invalid_argument{"Unsupported payloadLocation."};
        }
        if (spec.deviceIndex < 0)
        {
            throw std::invalid_argument{"deviceIndex must be >= 0 when payloadLocation is device memory."};
        }

        if (backend == PAYLOAD_BACKEND_CUDA_LINEAR)
        {
#if defined(MXL_HAS_CUDA) && MXL_HAS_CUDA
            if (!isCudaRuntimeAvailable())
            {
                throw std::runtime_error{"payload backend \"cuda-linear\" requested but no CUDA device is available."};
            }
            return std::make_unique<CudaLinearPayloadAllocator>(spec.deviceIndex);
#else
            throw std::runtime_error{"payload backend \"cuda-linear\" requested but MXL was built without CUDA support."};
#endif
        }

        if ((backend == PAYLOAD_BACKEND_PLACEHOLDER) || backend.empty())
        {
            return std::make_unique<DevicePayloadPlaceholderAllocator>(spec.deviceIndex);
        }

        throw std::invalid_argument{"Unsupported payload backend \"" + backend + "\"."};
    }

    std::unique_ptr<GrainPayloadAllocator> makeGrainPayloadAllocatorForFlow(std::filesystem::path const& flowDir,
        GrainPayloadAllocatorSpec const& fallbackSpec)
    {
        auto spec = fallbackSpec;
        auto const descriptorPath = makePayloadDescriptorFilePath(flowDir);
        if (std::filesystem::exists(descriptorPath))
        {
            auto in = std::ifstream{descriptorPath};
            if (!in)
            {
                throw std::filesystem::filesystem_error{
                    "Failed to read payload.json.", descriptorPath, std::make_error_code(std::errc::io_error)};
            }
            auto jsonValue = picojson::value{};
            auto const err = picojson::parse(jsonValue, in);
            if (!err.empty())
            {
                throw std::invalid_argument{"Invalid payload.json: " + err};
            }
            if (!jsonValue.is<picojson::object>())
            {
                throw std::invalid_argument{"payload.json root must be an object."};
            }
            auto const& root = jsonValue.get<picojson::object>();

            if (auto it = root.find("backend"); (it != root.end()) && it->second.is<std::string>())
            {
                spec.backend = it->second.get<std::string>();
            }
            if (auto it = root.find("deviceIndex"); (it != root.end()) && it->second.is<double>())
            {
                spec.deviceIndex = static_cast<int32_t>(it->second.get<double>());
            }
            if (auto it = root.find("grainSize"); (it != root.end()) && it->second.is<double>())
            {
                spec.logicalPayloadSize = static_cast<std::size_t>(it->second.get<double>());
            }
            if (auto it = root.find("location"); (it != root.end()) && it->second.is<std::string>())
            {
                auto const& loc = it->second.get<std::string>();
                if ((loc == "device") || (loc == "DEVICE") || (loc == "device_memory"))
                {
                    spec.location = MXL_PAYLOAD_LOCATION_DEVICE_MEMORY;
                }
                else
                {
                    spec.location = MXL_PAYLOAD_LOCATION_HOST_MEMORY;
                }
            }
        }

        return makeGrainPayloadAllocator(spec);
    }
}
