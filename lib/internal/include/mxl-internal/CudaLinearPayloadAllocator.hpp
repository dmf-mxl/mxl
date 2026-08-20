// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#include "GrainPayloadAllocator.hpp"

namespace mxl::lib
{
#if defined(MXL_HAS_CUDA) && MXL_HAS_CUDA
    /**
     * Device payload allocator using cudaMalloc'd linear buffers, shared across processes via
     * CUDA IPC memory handles published under \c payload/ in the flow directory.
     */
    class MXL_EXPORT CudaLinearPayloadAllocator final : public GrainPayloadAllocator
    {
    public:
        explicit CudaLinearPayloadAllocator(int32_t deviceIndex);

        ~CudaLinearPayloadAllocator() override;

        CudaLinearPayloadAllocator(CudaLinearPayloadAllocator const&) = delete;
        CudaLinearPayloadAllocator& operator=(CudaLinearPayloadAllocator const&) = delete;

        [[nodiscard]]
        mxlPayloadLocation location() const noexcept override;

        [[nodiscard]]
        int32_t deviceIndex() const noexcept override;

        [[nodiscard]]
        char const* backendName() const noexcept override;

        [[nodiscard]]
        std::size_t mappedPayloadBytes() const noexcept override;

        void attach(GrainPayloadAttachContext const& context) override;

        [[nodiscard]]
        mxlStatus viewAt(std::size_t slotIndex, Grain const* grain, mxlPayloadView* outView) const noexcept override;

    private:
        void release() noexcept;
        void createPayloads(GrainPayloadAttachContext const& context);
        void openPayloads(GrainPayloadAttachContext const& context);
        void writeDescriptor(GrainPayloadAttachContext const& context) const;

    private:
        int32_t _deviceIndex;
        std::size_t _logicalPayloadSize{0};
        bool _ownsDeviceMemory{false};
        /** True when pointers came from the process-local registry (must not free/close). */
        bool _borrowedFromLocalRegistry{false};
        std::vector<void*> _devicePtrs;
        std::vector<std::filesystem::path> _slotPaths;
    };

    [[nodiscard]]
    bool isCudaRuntimeAvailable() noexcept;
#else
    [[nodiscard]]
    inline bool isCudaRuntimeAvailable() noexcept
    {
        return false;
    }
#endif
}
