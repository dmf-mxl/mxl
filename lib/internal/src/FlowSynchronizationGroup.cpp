// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowSynchronizationGroup.hpp"
#include <mxl/time.h>
#include "mxl-internal/ContinuousFlowReader.hpp"
#include "mxl-internal/DiscreteFlowReader.hpp"
#include "mxl-internal/IndexConversion.hpp"

namespace mxl::lib
{
    FlowSynchronizationGroup::ListEntry::ListEntry(FlowReader const& reader, Variant variant)
        : reader{&reader}
        , minValidSlices{}
        , variant{variant}
        , maxObservedSourceDelay{}
    {
        auto const configInfo = reader.getFlowConfigInfo();
        grainRate = configInfo.common.grainRate;
    }

    FlowSynchronizationGroup::ListEntry::ListEntry(DiscreteFlowReader const& reader, std::uint16_t minValidSlices)
        : ListEntry{reader, Variant::Discrete}
    {
        this->minValidSlices = minValidSlices;
    }

    FlowSynchronizationGroup::ListEntry::ListEntry(ContinuousFlowReader const& reader)
        : ListEntry{reader, Variant::Continuous}
    {}

    void FlowSynchronizationGroup::addReader(DiscreteFlowReader const& reader, std::uint16_t minValidSlices)
    {
        auto prev = _readers.before_begin();
        for (auto current = std::next(prev); current != _readers.end(); ++current)
        {
            if (current->reader == &reader)
            {
                current->minValidSlices = minValidSlices;
                return;
            }
            prev = current;
        }

        _readers.emplace_after(prev, reader, minValidSlices);
    }

    void FlowSynchronizationGroup::addReader(ContinuousFlowReader const& reader)
    {
        auto prev = _readers.before_begin();
        for (auto current = std::next(prev); current != _readers.end(); ++current)
        {
            if (current->reader == &reader)
            {
                return;
            }
            prev = current;
        }

        _readers.emplace_after(prev, reader);
    }

    void FlowSynchronizationGroup::removeReader(FlowReader const& reader)
    {
        auto prev = _readers.before_begin();
        for (auto current = std::next(prev); current != _readers.end(); ++current)
        {
            if (current->reader == &reader)
            {
                _readers.erase_after(prev);
                return;
            }
            prev = current;
        }
    }

    mxlStatus FlowSynchronizationGroup::waitForDataAt(Timepoint originTime, Timepoint deadline) const
    {
        for (auto it = _readers.begin(); it != _readers.end(); /* Nothing */)
        {
            auto const current = it++;

            auto const expectedIndex = timestampToIndex(current->grainRate, originTime);
            auto const runtimeInfo = current->reader->getFlowRuntimeInfo();
            if (expectedIndex > runtimeInfo.headIndex)
            {
                auto result = mxlStatus{MXL_ERR_UNKNOWN};
                switch (current->variant)
                {
                    case Variant::Discrete:
                        result =
                            static_cast<DiscreteFlowReader const*>(current->reader)->waitForGrain(expectedIndex, current->minValidSlices, deadline);
                        break;

                    case Variant::Continuous:
                        result = static_cast<ContinuousFlowReader const*>(current->reader)->waitForSamples(expectedIndex, deadline);
                        break;
                }

                if (result == MXL_STATUS_OK)
                {
                    // If the current source delay of this flow exceeds any previosuly observed source delay of
                    // this flow we update the cached maximum and if this new maximum turns out to be bigger than
                    // the maximum source delay observed for the flow at the head of the list, we move this flow
                    // to the front, hoping that we can save blocking waits in the future.
                    auto const expectedArrivalTime = indexToTimestamp(current->grainRate, expectedIndex);
                    auto const currentTaiTime = currentTime(Clock::TAI);
                    if (currentTaiTime > expectedArrivalTime)
                    {
                        auto const sourceDelay = (currentTaiTime - expectedArrivalTime).value;
                        if (sourceDelay > current->maxObservedSourceDelay)
                        {
                            current->maxObservedSourceDelay = sourceDelay;
                            if (current->maxObservedSourceDelay > _readers.begin()->maxObservedSourceDelay)
                            {
                                _readers.splice_after(_readers.before_begin(), _readers, current);
                            }
                        }
                    }
                }
                else
                {
                    return result;
                }
            }
        }
        return MXL_STATUS_OK;
    }
}
