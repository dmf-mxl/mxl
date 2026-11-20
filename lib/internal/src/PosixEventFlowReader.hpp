// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief POSIX event reader declaration and reader-owned snapshot state.
 */

#pragma once
#include <vector>
#include "mxl-internal/EventFlowData.hpp"
#include "mxl-internal/EventFlowReader.hpp"

namespace mxl::lib
{
    class FlowManager;

    /** @brief POSIX event consumer with a single-thread cursor and an owned payload snapshot. */
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
        /// Close the access descriptor and release the snapshot and shared mappings.
        virtual ~PosixEventFlowReader() override;
        /** @return The owned common and event mappings. */
        virtual FlowData const& getFlowData() const override;
        /** @return A copy of the flow configuration and runtime fields. */
        virtual mxlFlowInfo getFlowInfo() const override;
        /** @return The immutable configuration of the mapped event flow. */
        virtual mxlFlowConfigInfo getFlowConfigInfo() const override;
        /** @return A copy of the runtime fields. */
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const override;
        /** @copydoc EventFlowReader::getEvent */
        virtual mxlStatus getEvent(Timepoint deadline, mxlEventInfo* info, std::uint8_t** payload) override;

    protected:
        /** @return True if the mapping exists and the flow's data file retains its recorded inode. */
        virtual bool isFlowValid() const override;

    private:
        /** @return True if the current data file's inode matches the mapping; requires _flowData. */
        bool isFlowValidImpl() const;

    private:
        std::unique_ptr<EventFlowData> _flowData; ///< Owned common metadata and read-only event mappings.
        int _accessFileFd;                        ///< Access notification descriptor, or -1 if unavailable.
        std::uint64_t _nextIndex;                 ///< Next queue entry for this reader's owning thread.
        std::vector<std::uint8_t> _snapshot;      ///< Payload storage reused on the next read attempt.
    };
}
