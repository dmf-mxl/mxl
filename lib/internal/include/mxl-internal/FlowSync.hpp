// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mxl/platform.h>
#include "Flow.hpp"
#include "SharedMemory.hpp"
#include "Timing.hpp"

namespace mxl::lib
{
    /**
     * Waits for and signals changes of the sync counter of a flow (FlowState::syncCounter), between threads of any process that has the flow
     * open.
     *
     * On Linux and macOS the counter is a futex. Windows has no futex that works across processes: WaitOnAddress() only wakes threads of the
     * process that calls WakeByAddress*(). There every flow has a wait queue of named kernel objects instead, see FlowSyncWindows.cpp.
     */
    class MXL_EXPORT FlowSync
    {
    public:
        /**
         * \param[in] flow The mapped flow segment. It must outlive this object.
         * \throw std::system_error If the wait queue of the flow cannot be created or opened (Windows).
         */
        explicit FlowSync(SharedMemoryInstance<Flow>& flow);

        FlowSync(FlowSync const&) = delete;
        FlowSync& operator=(FlowSync const&) = delete;

        ~FlowSync();

        /**
         * Wait until the sync counter differs from a value seen before, or a deadline passes.
         * \param[in] expected The value of the sync counter seen before.
         * \param[in] deadline Until when to wait, on Clock::Realtime.
         * \return true if the counter changed, false if the deadline passed.
         */
        bool waitUntilChanged(std::uint32_t expected, Timepoint deadline) const;

        /**
         * Increment the sync counter and wake all threads that wait for it to change.
         */
        void signal();

    private:
        /** The sync counter in the flow segment. */
        std::uint32_t* _counter;

#ifdef _WIN32
        class WaitQueue;
        /** The wait queue of the flow. */
        std::unique_ptr<WaitQueue> _queue;
#endif
    };
}
