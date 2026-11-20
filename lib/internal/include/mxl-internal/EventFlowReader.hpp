// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Backend contract for event queue consumers.
 */

#pragma once
#include "FlowReader.hpp"
#include "Timing.hpp"

namespace mxl::lib
{
    /**
     * @brief Backend contract for reading complete event entries or fragments in queue order.
     * Each reader has one owning thread; concurrent consumers must use independent readers.
     */
    class MXL_EXPORT EventFlowReader : public FlowReader
    {
    public:
        /**
         * @brief Copy the next available entry and advance this reader's cursor.
         * @param deadline Absolute realtime deadline for waiting; an expired deadline permits one attempt.
         * @param[out] info Metadata destination, valid on success; must not be null.
         * @param[out] payload Receives private snapshot storage on success; must not be null.
         * @return MXL_STATUS_OK, TOO_EARLY if unavailable by the deadline, TOO_LATE on
         * overwrite, or FLOW_INVALID for corrupt metadata or a detected removed flow.
         * @note Snapshot storage is reused by the next read attempt on this handle. Release must be coordinated with callers.
         */
        virtual mxlStatus getEvent(Timepoint deadline, mxlEventInfo* info, std::uint8_t** payload) = 0;

    protected:
        /**
         * @brief Initialize flow identity and domain using the base reader constructors.
         * @param flowId Identifier of the event flow.
         * @param domain Domain directory containing the flow.
         */
        using FlowReader::FlowReader;
    };
}
