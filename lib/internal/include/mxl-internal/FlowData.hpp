// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <vector>
#include <mxl/platform.h>
#include "Flow.hpp"
#include "FlowSync.hpp"
#include "SharedMemory.hpp"

namespace mxl::lib
{
    ///
    /// Simple structure holding the shared memory resources common to all types
    /// of flows.
    ///
    class MXL_EXPORT FlowData
    {
    public:
        constexpr bool isValid() const noexcept;
        constexpr explicit operator bool() const noexcept;

        constexpr AccessMode accessMode() const noexcept;
        constexpr bool created() const noexcept;
        constexpr std::size_t mappedSize() const noexcept;

        constexpr Flow* flow() noexcept;
        constexpr Flow const* flow() const noexcept;

        constexpr mxlFlowInfo* flowInfo() noexcept;
        constexpr mxlFlowInfo const* flowInfo() const noexcept;

        constexpr FlowState* flowState() noexcept;
        constexpr FlowState const* flowState() const noexcept;

        /** Waits for and signals changes of the sync counter in the flow state. */
        constexpr FlowSync& sync() noexcept;
        constexpr FlowSync const& sync() const noexcept;

        bool isExclusive() const;
        bool makeExclusive();

        /**
         * Pins the in-memory sections of every segment of this flow, see SectionPin. Used while the flow directory is renamed.
         */
        virtual std::vector<SectionPin> pinSections() const;

        virtual ~FlowData();

    protected:
        explicit FlowData(SharedMemoryInstance<Flow>&& flowSegement);
        FlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

    private:
        SharedMemoryInstance<Flow> _flow;
        /** Refers to the sync counter in _flow, so it is declared after it. */
        FlowSync _sync;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    inline FlowData::FlowData(SharedMemoryInstance<Flow>&& flowSegement)
        : _flow{std::move(flowSegement)}
        , _sync{_flow}
    {}

    constexpr bool FlowData::isValid() const noexcept
    {
        return _flow.isValid();
    }

    constexpr FlowData::operator bool() const noexcept
    {
        return _flow.isValid();
    }

    constexpr AccessMode FlowData::accessMode() const noexcept
    {
        return _flow.accessMode();
    }

    constexpr bool FlowData::created() const noexcept
    {
        return _flow.created();
    }

    constexpr std::size_t FlowData::mappedSize() const noexcept
    {
        return _flow.mappedSize();
    }

    constexpr Flow* FlowData::flow() noexcept
    {
        return _flow.get();
    }

    constexpr Flow const* FlowData::flow() const noexcept
    {
        return _flow.get();
    }

    constexpr mxlFlowInfo* FlowData::flowInfo() noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->info;
        }
        return nullptr;
    }

    constexpr mxlFlowInfo const* FlowData::flowInfo() const noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->info;
        }
        return nullptr;
    }

    constexpr FlowState* FlowData::flowState() noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->state;
        }
        return nullptr;
    }

    constexpr FlowState const* FlowData::flowState() const noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->state;
        }
        return nullptr;
    }

    constexpr FlowSync& FlowData::sync() noexcept
    {
        return _sync;
    }

    constexpr FlowSync const& FlowData::sync() const noexcept
    {
        return _sync;
    }
}
