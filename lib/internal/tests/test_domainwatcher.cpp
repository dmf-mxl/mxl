// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <list>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <uuid.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/DiscreteFlowWriter.hpp"
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl/flowinfo.h"
#include "../../tests/Utils.hpp"

using namespace mxl::lib;
namespace fs = std::filesystem;

namespace
{}

struct MockFlowFiles
{
    MockFlowFiles(fs::path domain, uuids::uuid const& id)
        : _domain{std::move(domain)}
        , _id{id}
        , _accessFileFd{-1}
    {
        auto flowDir = makeFlowDirectoryName(_domain, uuids::to_string(_id));
        std::filesystem::create_directories(flowDir);
        auto accessFilePath = makeFlowAccessFilePath(flowDir);
        auto dataFilePath = makeFlowDataFilePath(flowDir);

        _accessFileFd = ::open(accessFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
        if (_accessFileFd < 0)
        {
            auto const error = errno;
            throw std::system_error{error, std::system_category(), "Failed to open access file"};
        }

        _dataFileFd = ::open(dataFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);

        if (::ftruncate(_accessFileFd, sizeof(Grain)) < 0)
        {
            auto const error = errno;
            throw std::system_error{error, std::system_category(), "Failed truncate access file"};
        }

        if (::ftruncate(_dataFileFd, sizeof(Flow)) < 0)
        {
            auto const error = errno;
            throw std::system_error{error, std::system_category(), "Failed truncate access file"};
        }
    }

    ~MockFlowFiles()
    {
        if (_accessFileFd > -1)
        {
            (void)::close(_accessFileFd);
        }
        if (_dataFileFd > -1)
        {
            (void)::close(_dataFileFd);
        }

        auto flowDir = makeFlowDirectoryName(_domain, uuids::to_string(_id));
        fs::remove(makeFlowAccessFilePath(flowDir));
        fs::remove(makeFlowDataFilePath(flowDir));
    }

    void touch()
    {
        auto times = std::array<::timespec, 2>{
            timespec{.tv_sec = 0, .tv_nsec = UTIME_NOW },
            timespec{.tv_sec = 0, .tv_nsec = UTIME_OMIT}
        };
        if (auto ret = ::futimens(_accessFileFd, times.data()); ret < 0)
        {
            auto const error = errno;
            throw std::system_error(error, std::system_category(), "Failed to update flow access time");
        }
    }

    fs::path _domain;
    uuids::uuid _id;
    int _accessFileFd;
    int _dataFileFd;
};

struct MockWriter : mxl::lib::DiscreteFlowWriter
{
    MockWriter(std::filesystem::path const& domain, uuids::uuid id, DomainWatcher::ptr const& watcher)
        : mxl::lib::DiscreteFlowWriter(id)
        , _info{}
        , _id{id}
        , _watcher{watcher}
    {
        _watcher->addFlow(this, _id);
        _flowDataFd = ::open(makeFlowDataFilePath(domain, uuids::to_string(id)).c_str(), O_RDONLY | O_CLOEXEC, 0);
        if (_flowDataFd < 0)
        {
            auto const error = errno;
            throw std::system_error{error, std::system_category(), "Failed to open grain data file"};
        }

        _flowData = reinterpret_cast<Flow*>(::mmap(nullptr, sizeof(Flow), PROT_READ, MAP_SHARED, _flowDataFd, 0));
        if (_flowData == MAP_FAILED)
        {
            auto const error = errno;
            throw std::system_error{error, std::system_category(), "Failed to map grain data file"};
        }
    }

    virtual ~MockWriter() override
    {
        _watcher->removeFlow(this, _id);
        if ((_flowData != nullptr) && (_flowData != MAP_FAILED))
        {
            (void)::munmap(_flowData, sizeof(Flow));
        }

        if (_flowDataFd > -1)
        {
            (void)::close(_flowDataFd);
        }
    }

    [[noreturn]]
    virtual FlowData const& getFlowData() const override
    {
        // Implementation is not provided
        std::terminate();
    }

    virtual ::mxlFlowInfo getFlowInfo() const override
    {
        return _info;
    }

    virtual ::mxlFlowConfigInfo getFlowConfigInfo() const override
    {
        return _info.config;
    }

    virtual ::mxlFlowRuntimeInfo getFlowRuntimeInfo() const override
    {
        return _info.runtime;
    }

    virtual bool isExclusive() const override
    {
        return false;
    }

    virtual bool makeExclusive() override
    {
        return false;
    }

    /**
     * Get the grain info for a specific grain index without opening the grain for mutation.
     */
    [[noreturn]]
    virtual mxlGrainInfo getGrainInfo(std::uint64_t) const override
    {
        // Implementation is not provided
        std::terminate();
    };

    virtual mxlStatus openGrain(std::uint64_t, mxlGrainInfo*, std::uint8_t**) override
    {
        return MXL_STATUS_OK;
    }

    virtual mxlStatus commit(mxlGrainInfo const&) override
    {
        return MXL_STATUS_OK;
    }

    virtual mxlStatus cancel() override
    {
        return MXL_STATUS_OK;
    }

    bool checkReadTimeUpdated()
    {
        auto before = _lastReadTime;
        _lastReadTime = _flowData->info.runtime.lastReadTime;
        return before != _lastReadTime;
    }

    mxlFlowInfo _info;
    uuids::uuid _id;
    DomainWatcher::ptr _watcher;

    std::uint64_t _lastReadTime;
    int _flowDataFd;
    Flow* _flowData;
};

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Directory Watcher", "[directory watcher]")
{
    auto watcher = std::make_shared<DomainWatcher>(domain.native());

    auto flowId1 = *uuids::uuid::from_string("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    auto flowId2 = *uuids::uuid::from_string("6fbec3b1-1b0f-417d-9059-8b94a47197ed");

    auto mockFiles = MockFlowFiles{domain, flowId1};
    auto mockFiles2 = MockFlowFiles{domain, flowId2};

    auto mockWriter1 = std::make_unique<MockWriter>(domain, flowId1, watcher);
    auto mockWriter2 = std::make_unique<MockWriter>(domain, flowId1, watcher);
    REQUIRE(watcher->count(flowId1) == 2);
    REQUIRE(watcher->size() == 2);
    auto mockWriter3 = std::make_unique<MockWriter>(domain, flowId2, watcher);
    mockWriter1.reset();
    REQUIRE(watcher->count(flowId1) == 1);
    REQUIRE(watcher->count(flowId2) == 1);
    REQUIRE(watcher->size() == 2);
    mockWriter2.reset();
    REQUIRE(watcher->count(flowId1) == 0);
    REQUIRE(watcher->count(flowId2) == 1);
    REQUIRE(watcher->size() == 1);
    mockWriter3.reset();
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "DomainWatcher triggers callback on file modifications", "[domainwatcher]")
{
    // This test checks that modifying a watched file triggers the callback with correct UUID and WatcherType.
    auto watcher = std::make_shared<DomainWatcher>(domain);

    // Create a flow directory and files, then add watchers for both Reader and Writer types
    auto const flowId = *uuids::uuid::from_string("11111111-2222-3333-4444-555555555555"); // test UUID
    auto mockFlow = MockFlowFiles{domain, flowId};

    {
        auto writer1 = MockWriter{domain, flowId, watcher};
        auto writer2 = MockWriter{domain, flowId, watcher};

        // Give the watcher thread a moment to register the inotify watch
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Modify the main data file and expect a Writer event
        mockFlow.touch();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        REQUIRE(writer1.checkReadTimeUpdated());
        REQUIRE(writer2.checkReadTimeUpdated());
        REQUIRE(!writer1.checkReadTimeUpdated());
        REQUIRE(!writer2.checkReadTimeUpdated());

        // Modify the main data file and expect a Writer event
        mockFlow.touch();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        REQUIRE(writer1.checkReadTimeUpdated());
        REQUIRE(writer2.checkReadTimeUpdated());
    }

    REQUIRE(watcher->size() == 0);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "DomainWatcher thread start/stop behavior", "[domainwatcher]")
{
    // This test checks that modifying a watched file triggers the callback with correct UUID and WatcherType.
    auto watcher = std::make_shared<DomainWatcher>(domain);

    // Create a flow directory and files, then add watchers for both Reader and Writer types
    auto const flowId = *uuids::uuid::from_string("11111111-2222-3333-4444-555555555555"); // test UUID
    auto mockFlow = MockFlowFiles{domain, flowId};

    {
        auto mockWriter1 = MockWriter{domain, flowId, watcher};
        auto mockWriter2 = MockWriter{domain, flowId, watcher};
        REQUIRE(watcher->size() == 2);
        REQUIRE(watcher->count(flowId) == 2);

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        mockFlow.touch();

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        REQUIRE(mockWriter1.checkReadTimeUpdated());
        REQUIRE(mockWriter2.checkReadTimeUpdated());

        watcher->stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        mockFlow.touch();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        REQUIRE(!mockWriter1.checkReadTimeUpdated());
        REQUIRE(!mockWriter2.checkReadTimeUpdated());
    }
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "DomainWatcher error handling for invalid inputs", "[domainwatcher]")
{
    // Case 1: Using a valid domain path but adding a flow without creating the files
    auto watcher = std::make_shared<DomainWatcher>(domain);

    // Attempt to addFlow on a domain where the flow files don't exist (should throw because files are not present)
    uuids::uuid dummyId = *uuids::uuid::from_string("01234567-89ab-cdef-0123-456789abcdef");
    auto test = [&]()
    {
        auto mockWriter = MockWriter{domain, dummyId, watcher};
    };

    // The implementation throws std::system_error when inotify_add_watch fails
    REQUIRE_THROWS_AS(test(), std::system_error);

    // Removing a non-existant writer should not throw
    REQUIRE_NOTHROW(watcher->removeFlow(nullptr, dummyId));
    REQUIRE_NOTHROW(watcher->removeFlow(std::bit_cast<DiscreteFlowWriter*>(std::bit_cast<std::uintptr_t>(0x7f11251231231)), dummyId));
}

#ifdef __linux__ // This test uses /proc/self/fd which is Linux-specific
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "DomainWatcher cleans up file descriptors on destruction", "[domainwatcher]")
{
    // This test verifies that DomainWatcher closes its inotify/epoll descriptors upon destruction,
    // so no file descriptor leaks occur.
    // Helper: count open file descriptors (excluding the FD for /proc/self/fd itself)
    auto countOpenFDs = []()
    {
        auto count = size_t{0};
        for (auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
        {
            // Exclude the special entries "." and ".." if present
            auto const name = entry.path().filename().string();
            if (name == "." || name == "..")
            {
                continue;
            }
            count++;
        }
        return count;
    };

    auto const fdsBefore = countOpenFDs();
    { // scope to ensure DomainWatcher destruction
        auto watcher = std::make_shared<DomainWatcher>(domain);
        // Add a watch to force inotify initialization if not already done in constructor
        auto const flowId = *uuids::uuid::from_string("12345678-1234-5678-1234-567812345678");
        auto flowFiles = MockFlowFiles{domain, flowId};
        auto writer = MockWriter{domain, flowId, watcher};

        auto const fdsWhileActive = countOpenFDs();
        REQUIRE(fdsWhileActive > fdsBefore);
        // (No need to trigger events here; we just want the watcher to open its fds)
    } // DomainWatcher goes out of scope here, its destructor should close inotify/epoll fds
    auto const fdsAfter = countOpenFDs();

    // After destruction, the number of open fds should be back to the original count
    // (The DomainWatcher should have closed the two fds it opened: inotify and epoll)
    REQUIRE(fdsAfter == fdsBefore);
}
#endif // __linux__

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "DomainWatcher supports concurrent add/remove operations", "[domainwatcher]")
{
    // This test spawns multiple threads to add and remove flow watchers concurrently,
    // to ensure thread safety (no crashes or race conditions and correct final state).
    auto flowId = *uuids::uuid::from_string("12345678-1234-5678-1234-567812345678");
    auto mockFiles = MockFlowFiles{domain, flowId};

    std::random_device rd{};
    std::mt19937 rng{rd()};

    std::atomic<std::size_t> readyCount{0};
    std::atomic<bool> startSignal{false};

    constexpr auto iterations = 1024;
    constexpr auto nWorkers = 8;
    constexpr auto slots = 4;

    struct Worker
    {
        std::vector<std::optional<MockWriter>> writers;
        std::optional<std::thread> thread;
    };

    auto watcher = std::make_shared<DomainWatcher>(domain);
    auto work = [&](Worker* w)
    {
        readyCount.fetch_add(1);
        startSignal.wait(false);

        auto dist = std::uniform_int_distribution{std::size_t{0}, w->writers.size() - 1};

        for (std::size_t n = 0; n < iterations; ++n)
        {
            auto index = dist(rng);
            auto& slot = w->writers.at(index);
            if (slot)
            {
                slot.reset();
            }
            else
            {
                slot.emplace(domain, flowId, watcher);
            }

            if (index > (w->writers.size() / 2))
            {
                mockFiles.touch();
            }
        }
    };

    auto workers = std::list<Worker>{};
    for (auto i = 0; i < nWorkers; ++i)
    {
        auto& w = workers.emplace_back(std::vector<std::optional<MockWriter>>{slots}, std::nullopt);
        w.thread = std::thread{work, &w};
    }

    while (readyCount.load() != workers.size())
    {
    }

    startSignal.store(true);
    startSignal.notify_all();
    // work work work...
    std::ranges::for_each(workers,
        [](Worker& w)
        {
            w.thread->join();
            w.writers.clear();
        });

    REQUIRE(watcher->size() == 0);
    REQUIRE(watcher->count(flowId) == 0);
    {
        auto mockWriter = MockWriter{domain, flowId, watcher};
        REQUIRE(watcher->size() == 1);
        REQUIRE(watcher->count(flowId) == 1);
    }
    REQUIRE(watcher->size() == 0);
    REQUIRE(watcher->count(flowId) == 0);
}

TEST_CASE("DomainWatcher constructor throws on invalid domain path", "[domainwatcher]")
{
    // Ensure constructing on a non‐existent or non‐directory path throws
    auto const homeDir = std::getenv("HOME");
    REQUIRE(homeDir != nullptr);

    // Case: path does not exist
    auto bad1 = std::filesystem::path{homeDir} / "mxl_nonexistent_domain";
    remove_all(bad1);
    REQUIRE(!exists(bad1));
    REQUIRE_THROWS_AS((mxl::lib::DomainWatcher{bad1}), std::filesystem::filesystem_error);

    // Case: path exists but is a regular file
    auto bad2 = std::filesystem::path{homeDir} / "mxl_not_a_dir";
    remove_all(bad2);
    {
        auto touch = std::ofstream{bad2};
        touch << "notadir";
    }
    REQUIRE(exists(bad2));
    REQUIRE(is_regular_file(bad2));
    REQUIRE_THROWS_AS((mxl::lib::DomainWatcher{bad2}), std::filesystem::filesystem_error);

    // Clean up
    remove_all(bad2);
}
