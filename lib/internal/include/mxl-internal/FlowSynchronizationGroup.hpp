// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <forward_list>
#include <mxl/mxl.h>
#include <mxl/rational.h>
#include "Timing.hpp"

namespace mxl::lib
{
    class ContinuousFlowReader;
    class DiscreteFlowReader;
    class FlowReader;

    /**
     * A set of weak references to a flow readers that can be used to check for
     * data availability on all flows of the group at once.
     */
    class MXL_EXPORT FlowSynchronizationGroup
    {
    public:
        void addReader(DiscreteFlowReader const& reader, std::uint16_t minValidSlices);
        void addReader(ContinuousFlowReader const& reader);
        void removeReader(FlowReader const& reader);

        mxlStatus waitForDataAt(Timepoint originTime, Timepoint deadline) const;

    private:
        enum class Variant : std::uint8_t
        {
            Discrete,
            Continuous
        };

        struct ListEntry
        {
        public:
            /**
             * The reader representing the flow we operate on.
             */
            FlowReader const* reader;

            /**
             * For discrete flows this holds the chosen number of slices to wait
             * for.
             */
            std::uint16_t minValidSlices;

            /**
             * The type of reader encapsulated by this entry.
             */
            Variant variant;

            /**
             * Cached copy of the flows grain rate for localized access.
             */
            mxlRational grainRate;

            /**
             * The maximum source delay opportunistically observed by this
             * synchronization group.
             */
            std::int64_t maxObservedSourceDelay;

        public:
            explicit ListEntry(DiscreteFlowReader const& reader, std::uint16_t minValidSlices);
            explicit ListEntry(ContinuousFlowReader const& reader);

        private:
            explicit ListEntry(FlowReader const& reader, Variant variant);
        };

        using ReaderList = std::forward_list<ListEntry>;

    private:
        ReaderList mutable _readers;
    };
}
