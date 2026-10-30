// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/FlowSync.hpp"
#include "mxl-internal/SharedMemory.hpp"
#include "mxl-internal/Timing.hpp"

using namespace mxl::lib;

namespace
{
    /** A flow segment, created for the test and removed afterwards. Every mapping of it is a separate view, like in another process. */
    class FlowFile
    {
    public:
        explicit FlowFile(char const* name)
            : _path{std::filesystem::path{"."} / name}
        {
            std::filesystem::remove(_path);
            _creator = SharedMemoryInstance<Flow>{_path.string().c_str(), AccessMode::CREATE_READ_WRITE, 0U, LockMode::Shared};
        }

        FlowFile(FlowFile const&) = delete;
        FlowFile& operator=(FlowFile const&) = delete;

        ~FlowFile()
        {
            _creator = SharedMemoryInstance<Flow>{};
            auto error = std::error_code{};
            std::filesystem::remove(_path, error);
        }

        /** Map the flow again. */
        [[nodiscard]]
        SharedMemoryInstance<Flow> map() const
        {
            return SharedMemoryInstance<Flow>{_path.string().c_str(), AccessMode::READ_WRITE, 0U, LockMode::None};
        }

        /** \return The current value of the sync counter. */
        [[nodiscard]]
        std::uint32_t counter()
        {
            return std::atomic_ref{_creator.get()->state.syncCounter}.load(std::memory_order_acquire);
        }

    private:
        std::filesystem::path _path;
        SharedMemoryInstance<Flow> _creator;
    };

    /** A mapping of a flow and its FlowSync, as a reader or writer holds them. */
    struct Party
    {
        explicit Party(FlowFile const& file)
            : mapping{file.map()}
            , sync{mapping}
        {}

        SharedMemoryInstance<Flow> mapping;
        FlowSync sync;
    };

    [[nodiscard]]
    Timepoint deadlineIn(std::chrono::nanoseconds timeout)
    {
        return currentTime(Clock::Realtime) + Duration{timeout.count()};
    }
}

TEST_CASE("FlowSync: a signal wakes a waiter on another mapping", "[flow sync]")
{
    auto file = FlowFile{"mxl_flowsync_wake.bin"};
    auto writer = Party{file};
    auto reader = Party{file};

    auto const expected = file.counter();
    auto woken = std::atomic<std::int64_t>{0};
    auto changed = std::atomic<bool>{false};
    auto waiter = std::thread{[&]()
        {
            changed = reader.sync.waitUntilChanged(expected, deadlineIn(std::chrono::seconds{5}));
            woken = std::chrono::steady_clock::now().time_since_epoch().count();
        }};

    // Give the waiter time to block.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    auto const signaled = std::chrono::steady_clock::now().time_since_epoch().count();
    writer.sync.signal();
    waiter.join();

    REQUIRE(changed);
    REQUIRE(file.counter() == expected + 1);
    // A wake, not a poll: well below the 15.6 ms default system timer resolution of Windows.
    auto const latency = std::chrono::nanoseconds{woken - signaled};
    INFO("wake latency: " << std::chrono::duration_cast<std::chrono::microseconds>(latency).count() << " us");
    REQUIRE(latency < std::chrono::milliseconds{5});
}

TEST_CASE("FlowSync: a wait ends at its deadline", "[flow sync]")
{
    auto file = FlowFile{"mxl_flowsync_deadline.bin"};
    auto reader = Party{file};

    for (auto const timeout : {std::chrono::milliseconds{2}, std::chrono::milliseconds{20}})
    {
        auto const start = std::chrono::steady_clock::now();
        REQUIRE_FALSE(reader.sync.waitUntilChanged(file.counter(), deadlineIn(timeout)));
        auto const elapsed = std::chrono::steady_clock::now() - start;
        INFO("timeout: " << timeout.count() << " ms, waited: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " us");
        REQUIRE(elapsed >= timeout - std::chrono::microseconds{500});
        // Not rounded up to the system timer resolution.
        REQUIRE(elapsed < timeout + std::chrono::milliseconds{8});
    }
}

TEST_CASE("FlowSync: a changed counter does not wait", "[flow sync]")
{
    auto file = FlowFile{"mxl_flowsync_changed.bin"};
    auto writer = Party{file};
    auto reader = Party{file};

    auto const expected = file.counter();
    writer.sync.signal();
    auto const start = std::chrono::steady_clock::now();
    REQUIRE(reader.sync.waitUntilChanged(expected, deadlineIn(std::chrono::seconds{5})));
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds{1});
}

// Each round waits for the other thread, so a lost wake stops the exchange until the deadline of the wait.
TEST_CASE("FlowSync: no wake is lost between two threads", "[flow sync]")
{
    auto pingFile = FlowFile{"mxl_flowsync_ping.bin"};
    auto pongFile = FlowFile{"mxl_flowsync_pong.bin"};
    auto pingWriter = Party{pingFile};
    auto pingReader = Party{pingFile};
    auto pongWriter = Party{pongFile};
    auto pongReader = Party{pongFile};
    constexpr auto rounds = 20'000;

    auto ponged = std::atomic<int>{0};
    auto ponger = std::thread{[&]()
        {
            for (auto expected = std::uint32_t{0}; expected < rounds; ++expected)
            {
                if (!pingReader.sync.waitUntilChanged(expected, deadlineIn(std::chrono::seconds{2})))
                {
                    return;
                }
                pongWriter.sync.signal();
                ++ponged;
            }
        }};

    auto pinged = 0;
    for (auto expected = std::uint32_t{0}; expected < rounds; ++expected)
    {
        pingWriter.sync.signal();
        if (!pongReader.sync.waitUntilChanged(expected, deadlineIn(std::chrono::seconds{2})))
        {
            break;
        }
        ++pinged;
    }
    ponger.join();

    REQUIRE(pinged == rounds);
    REQUIRE(ponged == rounds);
}

// The writer signals the next round only after every waiter saw the last one. A waiter that misses a wake times out.
TEST_CASE("FlowSync: every waiter wakes on every signal", "[flow sync]")
{
    auto file = FlowFile{"mxl_flowsync_fanout.bin"};
    auto writer = Party{file};
    constexpr auto waiterCount = 8;
    constexpr auto rounds = 2'000;

    auto readers = std::vector<std::unique_ptr<Party>>{};
    for (auto i = 0; i < waiterCount; ++i)
    {
        readers.push_back(std::make_unique<Party>(file));
    }

    auto seen = std::atomic<int>{0};
    auto timeouts = std::atomic<int>{0};
    auto waiters = std::vector<std::thread>{};
    for (auto& reader : readers)
    {
        waiters.emplace_back(
            [&, sync = &reader->sync]()
            {
                for (auto expected = std::uint32_t{0}; expected < rounds; ++expected)
                {
                    if (!sync->waitUntilChanged(expected, deadlineIn(std::chrono::seconds{2})))
                    {
                        ++timeouts;
                        return;
                    }
                    ++seen;
                }
            });
    }

    for (auto round = 1; round <= rounds; ++round)
    {
        writer.sync.signal();
        auto const giveUp = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while ((seen < (round * waiterCount)) && (timeouts == 0) && (std::chrono::steady_clock::now() < giveUp))
        {
            std::this_thread::yield();
        }
        if (seen < (round * waiterCount))
        {
            break;
        }
    }
    for (auto& waiter : waiters)
    {
        waiter.join();
    }

    REQUIRE(timeouts == 0);
    REQUIRE(seen == (rounds * waiterCount));
}
