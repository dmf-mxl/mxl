// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Backend contract for the single event producer.
 */

#pragma once
#include "FlowWriter.hpp"

namespace mxl::lib
{
    /**
     * @brief Backend contract for a single event producer with private write staging.
     * Callers guarantee one writer per flow across all instances and processes,
     * and serialize the complete open/edit/commit-or-cancel transaction.
     */
    class MXL_EXPORT EventFlowWriter : public FlowWriter
    {
    public:
        /**
         * @brief Begin a write and reset metadata to the event defaults.
         * @param[out] info Receives initialized metadata; must not be null.
         * @param[out] payload Receives private writable storage; must not be null.
         * @return MXL_STATUS_OK, or MXL_ERR_INVALID_ARG if a write is already open.
         * @see mxlFlowWriterOpenEvent for field defaults and payload lifetime.
         */
        virtual mxlStatus openEvent(mxlEventInfo* info, std::uint8_t** payload) = 0;
        /**
         * @brief Publish the staged bytes as one complete event or fragment.
         * @param info Valid metadata whose timestamp is at least the last committed timestamp.
         * @return MXL_STATUS_OK on publication, INVALID_ARG on invalid metadata or state,
         * FLOW_INVALID on interrupted-tag reuse, or TOO_LATE on index exhaustion.
         * @note Failure leaves the transaction open for correction or cancellation.
         */
        virtual mxlStatus commit(mxlEventInfo const& info) = 0;
        /**
         * @brief Discard an open write without advancing the queue or timestamp.
         * @return MXL_STATUS_OK, or MXL_ERR_INVALID_ARG when no write is open.
         */
        virtual mxlStatus cancel() = 0;

    protected:
        /**
         * @brief Initialize flow identity and domain using the base writer constructors.
         * @param flowId Identifier of the event flow.
         * @param domain Domain directory containing the flow.
         */
        using FlowWriter::FlowWriter;
    };
}
