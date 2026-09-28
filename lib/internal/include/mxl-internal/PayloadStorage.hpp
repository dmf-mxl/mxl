// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/platform.h>

namespace mxl::lib
{
    class DiscreteFlowData;

    /**
     * Error in payload storage: invalid storage options, a backend that is not available, or a failure reported by
     * a backend. Carries the status code that the C API returns for it.
     */
    class MXL_EXPORT PayloadStorageError : public std::runtime_error
    {
    public:
        /**
         * \param[in] status The status code for the C API.
         * \param[in] what A description of the operation that failed.
         */
        PayloadStorageError(mxlStatus status, std::string const& what);

        /**
         * \return The status code for the C API.
         */
        [[nodiscard]]
        mxlStatus status() const noexcept;

    private:
        /** The status code for the C API. */
        mxlStatus _status;
    };

    /**
     * Holds the payload of the ring buffer slots of a discrete flow.
     *
     * The grain headers always stay in host shared memory. Only the payload is delegated to a storage object,
     * so that it can reside in other memory domains. Readers and writers select a slot by grain index as before
     * and ask the storage how that slot is represented.
     *
     * Implementations are internal to the SDK. The description of a slot must not change during the lifetime
     * of the storage object.
     */
    class MXL_EXPORT PayloadStorage
    {
    public:
        /** Destructor. Releases the payload memory owned by the storage, if any. */
        virtual ~PayloadStorage();

        PayloadStorage(PayloadStorage const&) = delete;
        PayloadStorage(PayloadStorage&&) = delete;
        PayloadStorage& operator=(PayloadStorage const&) = delete;
        PayloadStorage& operator=(PayloadStorage&&) = delete;

        /**
         * The storage type of every slot.
         * \return The storage type.
         */
        [[nodiscard]]
        virtual mxlPayloadStorageType type() const noexcept = 0;

        /**
         * The device that holds the payload.
         * \return The device index, or -1 if the payload is in host memory.
         */
        [[nodiscard]]
        virtual std::int32_t deviceIndex() const noexcept = 0;

        /**
         * The number of slots in the storage.
         * \return The number of slots. Equal to the grain count of the flow.
         */
        [[nodiscard]]
        virtual std::size_t slotCount() const noexcept = 0;

        /**
         * Describe the storage of one slot.
         * \param[in] slot The slot to describe. Must be less than slotCount().
         * \return The description of the slot, with version, size, storage type and slot set.
         * \throws std::out_of_range if slot is not less than slotCount().
         */
        [[nodiscard]]
        virtual mxlGrainStorage slotStorage(std::size_t slot) const = 0;

        /**
         * Complete the storage layout of the flow.
         *
         * The caller passes a default layout: version, size, slot count and plane count set, and planes stored
         * one after the other without padding between slices. The storage must set storageType, deviceIndex and
         * deviceUuid. It may change the offset, size and pitch of planes, for example when a device chose the
         * pitch of the buffers it allocated, and may fill the part of the reserved space defined for its storage
         * type. It must not change the other fields.
         *
         * \param[in,out] layout The default layout on input, the complete layout on output.
         * \throws std::exception if the layout cannot be described.
         */
        virtual void describeLayout(mxlGrainStorageLayout& layout) const = 0;

        /**
         * Map the storage of every slot into this process, so that slotStorage() does no further work.
         *
         * Opening a flow maps nothing. This call does the import, for example of device memory or of file
         * descriptors. It may be called more than once and from several threads. Calls after the first
         * successful one do nothing. If it was never called, slotStorage() maps on first use.
         *
         * \throws std::exception if the storage cannot be mapped in this process.
         */
        virtual void mapSlots() const = 0;

    protected:
        /** Default constructor for implementations. */
        PayloadStorage() = default;
    };

    /**
     * Payload storage in host shared memory. This is the storage used by all flows created before storage types
     * existed, and by all flows created without an explicit storage type.
     *
     * The payload of each slot directly follows its grain header in the shared memory segment of the grain.
     * The storage does not own that memory. The segments belong to the DiscreteFlowData the storage was built
     * from, which must outlive the storage.
     */
    class MXL_EXPORT
    HostPayloadStorage final : public PayloadStorage
    {
    public:
        /**
         * Build the storage from the grains of a discrete flow.
         * \param[in] flowData The flow data whose grains hold the payload. All grains must already be mapped.
         */
        explicit HostPayloadStorage(DiscreteFlowData& flowData);

        /** \see PayloadStorage::type() */
        [[nodiscard]]
        mxlPayloadStorageType type() const noexcept override;

        /** \see PayloadStorage::deviceIndex() */
        [[nodiscard]]
        std::int32_t deviceIndex() const noexcept override;

        /** \see PayloadStorage::slotCount() */
        [[nodiscard]]
        std::size_t slotCount() const noexcept override;

        /** \see PayloadStorage::slotStorage() */
        [[nodiscard]]
        mxlGrainStorage slotStorage(std::size_t slot) const override;

        /**
         * \see PayloadStorage::describeLayout()
         * Host storage keeps the default planes, which match the payload that follows each grain header.
         */
        void describeLayout(mxlGrainStorageLayout& layout) const override;

        /**
         * \see PayloadStorage::mapSlots()
         * Host storage is mapped together with the grain headers, so this does nothing.
         */
        void mapSlots() const override;

    private:
        /** Host address of the payload of each slot, indexed by slot. */
        std::vector<void*> _slotPayloads;
    };

    /**
     * Compute the storage layout of a discrete flow from its configuration and its payload storage.
     *
     * Builds the default layout, in which planes follow each other without padding and the pitch of plane i is
     * sliceSizes[i], so that its size is sliceSizes[i] * totalSlices. Then lets the storage complete it with
     * PayloadStorage::describeLayout(), and checks the result.
     *
     * \param[in] config The discrete configuration of the flow, which gives the slice size of each plane.
     * \param[in] totalSlices The number of slices in a grain of the flow.
     * \param[in] storage The payload storage of the flow.
     * \return The storage layout.
     * \throws std::logic_error if the storage changed fields it must not change, or described a plane that
     *      cannot hold its slices.
     */
    [[nodiscard]]
    MXL_EXPORT
    mxlGrainStorageLayout makeGrainStorageLayout(mxlDiscreteFlowConfigInfo const& config, std::uint16_t totalSlices, PayloadStorage const& storage);
}
