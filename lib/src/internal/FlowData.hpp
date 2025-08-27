// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include "Flow.hpp"
#include "SharedMemory.hpp"

namespace mxl::lib
{
    ///
    /// Simple structure holding the shared memory resources common to all types
    /// of flows.
    ///
    class FlowData
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

        virtual ~FlowData();

    protected:
        constexpr explicit FlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;
        FlowData(char const* flowFilePath, AccessMode mode);

    private:
        SharedMemoryInstance<Flow> _flow;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    constexpr FlowData::FlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept
        : _flow{std::move(flowSegement)}
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
}
