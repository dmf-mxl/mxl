// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief POSIX event reader declaration and per-thread snapshot state.
 */

#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include "mxl-internal/EventFlowData.hpp"
#include "mxl-internal/EventFlowReader.hpp"

namespace mxl::lib
{
    class FlowManager;

    /** @brief POSIX event consumer with an atomic cursor and per-thread payload snapshots. */
    class PosixEventFlowReader final : public EventFlowReader
    {
    public:
        /**
         * @brief Attach a reader at the oldest retained entry.
         * @param manager Manager supplying the flow domain.
         * @param flowId Identifier of the mapped flow.
         * @param data Mappings whose ownership transfers to this reader.
         */
        PosixEventFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<EventFlowData>&& data);
        /// Close the access descriptor and release snapshots and shared mappings.
        virtual ~PosixEventFlowReader() override;
        /** @return The owned common and event mappings. */
        virtual FlowData const& getFlowData() const override;
        /** @return Flow configuration and individually loaded atomic runtime fields. */
        virtual mxlFlowInfo getFlowInfo() const override;
        /** @return The immutable configuration of the mapped event flow. */
        virtual mxlFlowConfigInfo getFlowConfigInfo() const override;
        /** @return Runtime fields loaded atomically, without a combined transaction. */
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const override;
        /** @copydoc EventFlowReader::getEvent */
        virtual mxlStatus getEvent(Timepoint deadline, mxlEventInfo* info, std::uint8_t** payload) override;

    protected:
        /** @return True if the mapping exists and the flow's data file retains its recorded inode. */
        virtual bool isFlowValid() const override;

    private:
        /** @return True if the current data file's inode matches the mapping; requires _flowData. */
        bool isFlowValidImpl() const;
        /**
         * @brief Find or allocate the calling thread's payload buffer under a local mutex.
         * @return Storage with capacity for one entry, stable until reader destruction.
         * @throws std::bad_alloc Snapshot storage cannot be allocated.
         */
        std::vector<std::uint8_t>& threadSnapshot();

    private:
        std::unique_ptr<EventFlowData> _flowData;                        ///< Owned common metadata and read-only event mappings.
        int _accessFileFd;                                               ///< Access notification descriptor, or -1 if unavailable.
        std::atomic<std::uint64_t> _nextIndex;                           ///< Next queue entry for callers sharing this handle.
        std::mutex _snapshotMutex;                                       ///< Protects insertion and lookup in the per-thread buffer map.
        std::map<std::thread::id, std::vector<std::uint8_t>> _snapshots; ///< Payload buffers retained until reader destruction.
    };
}
