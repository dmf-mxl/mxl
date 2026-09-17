// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief POSIX event writer declaration and private publication state.
 */

#pragma once
#include <vector>
#include "mxl-internal/EventFlowData.hpp"
#include "mxl-internal/EventFlowWriter.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * @brief POSIX single-producer writer with private staging and monotonic timestamps.
     * The caller guarantees one writer per flow and serializes transactions; readers may run concurrently.
     */
    class PosixEventFlowWriter final : public EventFlowWriter
    {
    public:
        /**
         * @brief Attach the sole writer and restore any last committed timestamp.
         * @param manager Manager supplying the flow domain.
         * @param flowId Identifier of the mapped flow.
         * @param data Event mappings whose ownership transfers to this writer.
         * @param watcher Shared watcher used for reader access notifications.
         * @pre The caller guarantees that no other writer owns this flow.
         * @throws std::runtime_error The retained head is invalid.
         * @throws std::exception Allocation or watcher registration fails.
         */
        PosixEventFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<EventFlowData>&& data,
            std::shared_ptr<DomainWatcher> const& watcher);
        /// Unregister from the watcher and release mappings and the common flow lock.
        ~PosixEventFlowWriter() override;
        /** @return The owned common and event mappings. */
        FlowData const& getFlowData() const override;
        /** @return Flow configuration and individually loaded atomic runtime fields. */
        mxlFlowInfo getFlowInfo() const override;
        /** @return Immutable event flow configuration. */
        mxlFlowConfigInfo getFlowConfigInfo() const override;
        /** @return Runtime fields loaded atomically, without a combined transaction. */
        mxlFlowRuntimeInfo getFlowRuntimeInfo() const override;
        /** @return Mutable access to the owned common and event mappings. */
        FlowData& getFlowData() override;
        /** @copydoc EventFlowWriter::openEvent */
        mxlStatus openEvent(mxlEventInfo* info, std::uint8_t** payload) override;
        /** @copydoc EventFlowWriter::commit */
        mxlStatus commit(mxlEventInfo const& info) override;
        /** @copydoc EventFlowWriter::cancel */
        mxlStatus cancel() override;
        /** @return True if the common flow metadata file is exclusively locked by this mapping. */
        bool isExclusive() const override;
        /** @return True if exclusive common-metadata ownership is acquired, allowing flow deletion. */
        bool makeExclusive() override;

    private:
        std::unique_ptr<EventFlowData> _flowData; ///< Owns common metadata, ring mappings and the common flow lock.

        bool _open{};                             ///< True while one caller-owned write transaction is open.
        std::uint64_t _lastTimestamp{};           ///< Last committed timestamp, restored from the ring on attachment.
        std::vector<std::uint8_t> _payload;       ///< Private staging buffer; publication copies only eventSize bytes.
        std::shared_ptr<DomainWatcher> _watcher;  ///< Watcher kept alive while the writer is registered.
    };
}
