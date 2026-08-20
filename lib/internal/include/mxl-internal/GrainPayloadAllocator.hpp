// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <mxl/flowinfo.h>
#include <mxl/mxl.h>
#include <mxl/platform.h>
#include "Flow.hpp"
#include "SharedMemory.hpp"

namespace mxl::lib
{
    /** Well-known payload backend names used in flow options / payload.json. */
    inline constexpr char const* PAYLOAD_BACKEND_HOST = "host";
    inline constexpr char const* PAYLOAD_BACKEND_CUDA_LINEAR = "cuda-linear";
    inline constexpr char const* PAYLOAD_BACKEND_PLACEHOLDER = "placeholder";

    /**
     * Context passed when an allocator is bound to a concrete flow directory.
     */
    struct GrainPayloadAttachContext
    {
        std::filesystem::path flowDir;
        std::size_t grainCount{0};
        std::size_t logicalPayloadSize{0};
        AccessMode accessMode{AccessMode::READ_ONLY};
    };

    /**
     * Strategy that owns (or will own) the payload bytes of a discrete-flow grain ring.
     *
     * Grain headers remain file-backed host shared memory. Implementations decide whether payload
     * bytes are embedded in the same mapping (host) or live elsewhere (device).
     */
    class MXL_EXPORT GrainPayloadAllocator
    {
    public:
        virtual ~GrainPayloadAllocator() = default;

        /** Where payloads for this flow are located. */
        [[nodiscard]]
        virtual mxlPayloadLocation location() const noexcept = 0;

        /** Device index for device payloads; -1 for host. */
        [[nodiscard]]
        virtual int32_t deviceIndex() const noexcept = 0;

        /** Backend name written to / read from payload.json (e.g. "host", "cuda-linear"). */
        [[nodiscard]]
        virtual char const* backendName() const noexcept = 0;

        /**
         * Extra bytes to map together with each \c GrainHeader when creating grain files.
         * Return 0 when the payload is not embedded in the grain mapping (device memory path).
         */
        [[nodiscard]]
        virtual std::size_t mappedPayloadBytes() const noexcept = 0;

        /**
         * Bind this allocator to a flow directory.
         *
         * On create (\c AccessMode::CREATE_READ_WRITE): allocate payload resources and publish
         * descriptors under the flow directory.
         * On open: import previously published descriptors.
         */
        virtual void attach(GrainPayloadAttachContext const& context) = 0;

        /**
         * Build a payload view for ring slot \p slotIndex.
         *
         * \return MXL_STATUS_OK on success. MXL_ERR_UNSUPPORTED_OPERATION when the backing store
         *         exists only as metadata (placeholder device allocator).
         */
        [[nodiscard]]
        virtual mxlStatus viewAt(std::size_t slotIndex, Grain const* grain, mxlPayloadView* outView) const noexcept = 0;
    };

    /**
     * Default allocator: payload bytes are embedded immediately after the grain header in the
     * same host mmap (preserves today's on-disk layout).
     */
    class MXL_EXPORT HostMappedPayloadAllocator final : public GrainPayloadAllocator
    {
    public:
        explicit HostMappedPayloadAllocator(std::size_t mappedPayloadBytes) noexcept;

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
        std::size_t _mappedPayloadBytes;
    };

    /**
     * Placeholder for device flows without a concrete device allocator.
     * Creates header-only grain mappings; payload access returns unsupported.
     */
    class MXL_EXPORT DevicePayloadPlaceholderAllocator final : public GrainPayloadAllocator
    {
    public:
        explicit DevicePayloadPlaceholderAllocator(int32_t deviceIndex) noexcept;

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
        int32_t _deviceIndex;
    };

    /**
     * Factory arguments for constructing a payload allocator.
     */
    struct GrainPayloadAllocatorSpec
    {
        mxlPayloadLocation location{MXL_PAYLOAD_LOCATION_HOST_MEMORY};
        int32_t deviceIndex{-1};
        std::size_t logicalPayloadSize{0};
        /** Backend name: "host", "cuda-linear", "placeholder", or empty for defaults. */
        std::string backend;
    };

    /**
     * Select an allocator from flow options / stored flow metadata.
     *
     * For device location with backend "cuda-linear", requires the library to have been built
     * with CUDA support. Otherwise falls back to the device placeholder (or throws if an
     * explicit unsupported backend was requested).
     */
    [[nodiscard]]
    MXL_EXPORT std::unique_ptr<GrainPayloadAllocator> makeGrainPayloadAllocator(GrainPayloadAllocatorSpec const& spec);

    /**
     * Read payload.json from an existing flow directory, if present, and build an allocator.
     * When payload.json is missing, falls back to \p fallbackSpec (typically from flow config).
     */
    [[nodiscard]]
    MXL_EXPORT std::unique_ptr<GrainPayloadAllocator> makeGrainPayloadAllocatorForFlow(std::filesystem::path const& flowDir,
        GrainPayloadAllocatorSpec const& fallbackSpec);
}
