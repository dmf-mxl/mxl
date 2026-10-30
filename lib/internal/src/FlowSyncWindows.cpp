// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// The Windows implementation of FlowSync.hpp. FlowSync.cpp holds the POSIX one.
//
// WaitOnAddress() only wakes threads of the process that calls WakeByAddress*(), and Windows has no other futex. Each flow therefore has a
// wait queue: a table of slots in a named section, and a named auto-reset event per slot. A thread that waits for the sync counter to change
// takes a free slot, checks the counter again and waits on the event of its slot. A writer that changed the counter sets the event of every
// taken slot. Each waiter has its own event, so a waiter cannot consume the wake of another, and a writer never waits for readers.
//
// Taking a slot and incrementing the counter are sequentially consistent read-modify-write operations, each followed by a sequentially
// consistent load of the other word. So either the writer sees the taken slot and sets its event, or the waiter sees the new counter and does
// not wait. A wake can arrive for a waiter that has already left its slot. The next waiter of the slot then returns from its wait at once,
// finds the counter unchanged and waits again.
//
// The objects are named after the in-memory section of the flow segment, so they belong to one flow and are in the Local\ namespace like it.
// They exist while a process has the flow open.

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <bit>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <windows.h>
#include "mxl-internal/FlowSync.hpp"
#include "mxl-internal/Logging.hpp"
#include "Deferred.hpp"
#include "WindowsTimer.hpp"

namespace mxl::lib
{
    namespace
    {
        constexpr auto SLOT_WORDS = std::size_t{4};
        constexpr auto SLOTS_PER_WORD = std::size_t{64};
        /** The number of threads, in all processes, that can wait for one flow at the same time. */
        constexpr auto SLOT_COUNT = SLOT_WORDS * SLOTS_PER_WORD;

        /** The shared part of a wait queue. A new section is filled with zeros, so a new table has no waiters. */
        struct WaitTable
        {
            /** One bit per slot, set while a thread waits in the slot. */
            std::array<std::uint64_t, SLOT_WORDS> taken;
            /** The ID of the process of the thread that waits in each slot, 0 while the slot is taken or released. */
            std::array<std::uint32_t, SLOT_COUNT> owner;
        };

        [[nodiscard]]
        std::system_error lastError(char const* what)
        {
            return std::system_error{static_cast<int>(::GetLastError()), std::system_category(), what};
        }

        /** \return Whether the process with the given ID has exited. */
        [[nodiscard]]
        bool hasExited(DWORD processId) noexcept
        {
            auto const process = ::OpenProcess(SYNCHRONIZE, FALSE, processId);
            if (process == nullptr)
            {
                // A process that exists but may not be opened reports ERROR_ACCESS_DENIED.
                return ::GetLastError() == ERROR_INVALID_PARAMETER;
            }
            auto const exited = (::WaitForSingleObject(process, 0) == WAIT_OBJECT_0);
            ::CloseHandle(process);
            return exited;
        }

        /**
         * Wait until an event is set or a time out expires. The time out is measured with the high resolution timer of the thread, because the
         * time out of a wait function is rounded to the system timer resolution.
         * \return Whether the event was set.
         */
        bool waitForEvent(HANDLE event, Duration timeout)
        {
            if (auto const timer = static_cast<HANDLE>(threadTimer()); timer != nullptr)
            {
                // Negative due times are relative, in 100 ns units.
                auto dueTime = LARGE_INTEGER{};
                dueTime.QuadPart = -((timeout.value + 99) / 100);
                if (::SetWaitableTimer(timer, &dueTime, 0, nullptr, nullptr, FALSE) == FALSE)
                {
                    throw lastError("Could not set the timer of a flow wait.");
                }

                // If both are signaled, the result names the event, which comes first.
                auto const handles = std::array<HANDLE, 2>{event, timer};
                auto const result = ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
                if (result == WAIT_FAILED)
                {
                    throw lastError("Could not wait for a flow.");
                }
                return result == WAIT_OBJECT_0;
            }

            // Windows versions before 10 1803 have no high resolution timer. The time out is then rounded to the system timer resolution.
            constexpr auto maxMilliseconds = std::int64_t{INFINITE - 1};
            auto const milliseconds = (timeout.value + 999'999) / 1'000'000;
            auto const result = ::WaitForSingleObject(event, static_cast<DWORD>((milliseconds < maxMilliseconds) ? milliseconds : maxMilliseconds));
            if (result == WAIT_FAILED)
            {
                throw lastError("Could not wait for a flow.");
            }
            return result == WAIT_OBJECT_0;
        }
    }

    /** The wait queue of a flow, opened by one FlowSync. */
    class FlowSync::WaitQueue
    {
    public:
        /**
         * Create or open the wait queue of a flow.
         * \param[in] name The name of the in-memory section of the flow segment.
         */
        explicit WaitQueue(std::wstring name);

        WaitQueue(WaitQueue const&) = delete;
        WaitQueue& operator=(WaitQueue const&) = delete;

        ~WaitQueue();

        /** See FlowSync::waitUntilChanged(). */
        bool wait(std::uint32_t* counter, std::uint32_t expected, Timepoint deadline);

        /** Wake every thread that waits in the queue. Failures are logged: the threads then wake at their deadline. */
        void wakeAll() noexcept;

    private:
        /**
         * Take a free slot. If no slot is free, take over a slot whose thread is gone with its process.
         * \return The slot.
         * \throw std::runtime_error If all slots are taken by threads that wait.
         */
        std::size_t takeSlot();

        /** Give a slot taken by takeSlot() back. */
        void releaseSlot(std::size_t slot) noexcept;

        /**
         * Get the event of a slot, created or opened on first use. Every process gets the same event for a slot.
         * \throw std::system_error If the event cannot be created or opened.
         */
        HANDLE event(std::size_t slot);

        /** The name of the in-memory section of the flow segment. */
        std::wstring _name;
        /** The section that holds the table. */
        HANDLE _section;
        /** The mapped table. */
        WaitTable* _table;
        /** The events of the slots opened by this process, or null. */
        std::array<std::atomic<HANDLE>, SLOT_COUNT> _events;
    };

    FlowSync::WaitQueue::WaitQueue(std::wstring name)
        : _name{std::move(name)}
        , _section{nullptr}
        , _table{nullptr}
        , _events{}
    {
        // Creates the section, or opens it if another FlowSync of the flow has created it.
        _section = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(WaitTable), (_name + L"-wait").c_str());
        if (_section == nullptr)
        {
            throw lastError("Could not create the wait queue of a flow.");
        }

        _table = static_cast<WaitTable*>(::MapViewOfFile(_section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(WaitTable)));
        if (_table == nullptr)
        {
            auto const error = lastError("Could not map the wait queue of a flow.");
            ::CloseHandle(_section);
            throw error;
        }
    }

    FlowSync::WaitQueue::~WaitQueue()
    {
        for (auto& event : _events)
        {
            if (auto const handle = event.load(std::memory_order_relaxed); handle != nullptr)
            {
                ::CloseHandle(handle);
            }
        }
        (void)::UnmapViewOfFile(_table);
        (void)::CloseHandle(_section);
    }

    bool FlowSync::WaitQueue::wait(std::uint32_t* counter, std::uint32_t expected, Timepoint deadline)
    {
        auto const value = std::atomic_ref{*counter};
        while (value.load(std::memory_order_acquire) == expected)
        {
            auto const now = currentTime(Clock::Realtime);
            if (now >= deadline)
            {
                return false;
            }

            auto const slot = takeSlot();
            auto const release = defer([this, slot]() noexcept { releaseSlot(slot); });
            // Open the event before the counter is checked again: a writer that sees the slot must find the event of this waiter.
            auto const signal = event(slot);
            if (value.load(std::memory_order_seq_cst) != expected)
            {
                return true;
            }
            (void)waitForEvent(signal, deadline - now);
        }
        return true;
    }

    void FlowSync::WaitQueue::wakeAll() noexcept
    {
        for (auto word = std::size_t{0}; word < SLOT_WORDS; ++word)
        {
            auto bits = std::atomic_ref{_table->taken[word]}.load(std::memory_order_seq_cst);
            while (bits != 0)
            {
                auto const slot = (word * SLOTS_PER_WORD) + static_cast<std::size_t>(std::countr_zero(bits));
                bits &= bits - 1;
                try
                {
                    if (::SetEvent(event(slot)) == FALSE)
                    {
                        throw lastError("Could not set the wait event of a flow.");
                    }
                }
                catch (std::exception const& e)
                {
                    MXL_ERROR("Failed to wake a thread that waits for a flow: {}", e.what());
                }
            }
        }
    }

    std::size_t FlowSync::WaitQueue::takeSlot()
    {
        auto const self = static_cast<std::uint32_t>(::GetCurrentProcessId());
        for (auto word = std::size_t{0}; word < SLOT_WORDS; ++word)
        {
            auto const taken = std::atomic_ref{_table->taken[word]};
            auto bits = taken.load(std::memory_order_relaxed);
            while (bits != ~std::uint64_t{0})
            {
                auto const bit = static_cast<std::size_t>(std::countr_one(bits));
                if (taken.compare_exchange_weak(bits, bits | (std::uint64_t{1} << bit), std::memory_order_seq_cst, std::memory_order_relaxed))
                {
                    auto const slot = (word * SLOTS_PER_WORD) + bit;
                    std::atomic_ref{_table->owner[slot]}.store(self, std::memory_order_release);
                    return slot;
                }
            }
        }

        // A thread that dies while it waits, with its process, leaves its slot taken. Its bit stays set, so the slot can be handed over.
        for (auto slot = std::size_t{0}; slot < SLOT_COUNT; ++slot)
        {
            auto const owner = std::atomic_ref{_table->owner[slot]};
            auto processId = owner.load(std::memory_order_acquire);
            if ((processId != 0) && (processId != self) && hasExited(processId) &&
                owner.compare_exchange_strong(processId, self, std::memory_order_seq_cst, std::memory_order_relaxed))
            {
                return slot;
            }
        }

        throw std::runtime_error{"Too many threads wait for the flow."};
    }

    void FlowSync::WaitQueue::releaseSlot(std::size_t slot) noexcept
    {
        // Clear the owner first. Once the bit is clear, another thread can take the slot and set its own owner.
        std::atomic_ref{_table->owner[slot]}.store(0, std::memory_order_relaxed);
        std::atomic_ref{_table->taken[slot / SLOTS_PER_WORD]}.fetch_and(~(std::uint64_t{1} << (slot % SLOTS_PER_WORD)), std::memory_order_release);
    }

    HANDLE FlowSync::WaitQueue::event(std::size_t slot)
    {
        auto& cached = _events[slot];
        if (auto const handle = cached.load(std::memory_order_acquire); handle != nullptr)
        {
            return handle;
        }

        // Creates the event, or opens it if another process (or thread) has created it.
        auto const handle = ::CreateEventW(nullptr, FALSE, FALSE, (_name + L"-wait-" + std::to_wstring(slot)).c_str());
        if (handle == nullptr)
        {
            throw lastError("Could not create the wait event of a flow.");
        }
        auto expected = HANDLE{nullptr};
        if (!cached.compare_exchange_strong(expected, handle, std::memory_order_acq_rel))
        {
            // Another thread of this process opened it first.
            ::CloseHandle(handle);
            return expected;
        }
        return handle;
    }

    FlowSync::FlowSync(SharedMemoryInstance<Flow>& flow)
        : _counter{&flow.get()->state.syncCounter}
        , _queue{std::make_unique<WaitQueue>(flow.sectionName())}
    {}

    FlowSync::~FlowSync() = default;

    bool FlowSync::waitUntilChanged(std::uint32_t expected, Timepoint deadline) const
    {
        return _queue->wait(_counter, expected, deadline);
    }

    void FlowSync::signal()
    {
        std::atomic_ref{*_counter}.fetch_add(1, std::memory_order_seq_cst);
        _queue->wakeAll();
    }
}
