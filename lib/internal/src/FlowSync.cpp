// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// The POSIX implementation of FlowSync.hpp. FlowSyncWindows.cpp holds the Windows one.

#include "mxl-internal/FlowSync.hpp"
#include <atomic>
#include "mxl-internal/Sync.hpp"

namespace mxl::lib
{
    FlowSync::FlowSync(SharedMemoryInstance<Flow>& flow)
        : _counter{&flow.get()->state.syncCounter}
    {}

    FlowSync::~FlowSync() = default;

    bool FlowSync::waitUntilChanged(std::uint32_t expected, Timepoint deadline) const
    {
        return mxl::lib::waitUntilChanged(_counter, expected, deadline);
    }

    void FlowSync::signal()
    {
        std::atomic_ref{*_counter}.fetch_add(1, std::memory_order_seq_cst);
        wakeAll(_counter);
    }
}
