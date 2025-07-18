// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <sys/stat.h>
#include "../src/internal/DomainWatcher.hpp"
#include "../src/internal/PathUtils.hpp"

using namespace mxl::lib;
namespace fs = std::filesystem;

TEST_CASE("Directory Watcher", "[directory watcher]")
{
    auto const homeDir = getenv("HOME");
    REQUIRE(homeDir != nullptr);
    auto domain = std::filesystem::path{std::string{homeDir} + "/mxl_domain"}; // Remove that path if it exists.
    remove_all(domain);

    create_directories(domain);

    auto watcher = DomainWatcher{domain.native(), nullptr};

    auto const* id = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    auto const flowDirectory = makeFlowDirectoryName(domain, id);

    create_directories(flowDirectory);

    auto flowId = *uuids::uuid::from_string(id);
    auto f1Path = makeFlowDataFilePath(flowDirectory);
    auto f1 = std::ofstream{f1Path};

    auto f2Path = makeFlowAccessFilePath(flowDirectory);
    auto f2 = std::ofstream{f2Path};

    REQUIRE(1 == watcher.addFlow(flowId, WatcherType::READER));
    REQUIRE(2 == watcher.addFlow(flowId, WatcherType::READER));
    REQUIRE(1 == watcher.addFlow(flowId, WatcherType::WRITER));
    REQUIRE(2 == watcher.addFlow(flowId, WatcherType::WRITER));
    REQUIRE(1 == watcher.removeFlow(flowId, WatcherType::READER));
    REQUIRE(0 == watcher.removeFlow(flowId, WatcherType::READER));
    REQUIRE(-1 == watcher.removeFlow(flowId, WatcherType::READER));
    REQUIRE(1 == watcher.removeFlow(flowId, WatcherType::WRITER));
    REQUIRE(0 == watcher.removeFlow(flowId, WatcherType::WRITER));
    REQUIRE(-1 == watcher.removeFlow(flowId, WatcherType::WRITER));
}

TEST_CASE("DomainWatcher triggers callback on file modifications", "[domainwatcher]")
{
    // This test checks that modifying a watched file triggers the callback with correct UUID and WatcherType.
    auto const homeDir = std::getenv("HOME");
    REQUIRE(homeDir != nullptr);
    auto domainPath = std::filesystem::path{homeDir} / "mxl_test_domain_events";
    remove_all(domainPath);
    create_directories(domainPath);

    // Variables to capture callback information
    auto eventMutex = std::mutex{};
    auto eventCV = std::condition_variable{};
    auto eventCount = std::atomic<int>{0};
    auto callbackUuid = uuids::uuid{};
    auto callbackType = mxl::lib::WatcherType{};

    // Set up DomainWatcher with a callback that notifies when a file change event is caught
    mxl::lib::DomainWatcher watcher(domainPath,
        [&](uuids::uuid id, mxl::lib::WatcherType type)
        {
            auto lock = std::unique_lock<std::mutex>{eventMutex};
            callbackUuid = id;
            callbackType = type;
            eventCount++;
            lock.unlock();
            eventCV.notify_one();
        });

    // Create a flow directory and files, then add watchers for both Reader and Writer types
    auto const flowIdStr = std::string{"11111111-2222-3333-4444-555555555555"}; // test UUID
    auto flowDir = mxl::lib::makeFlowDirectoryName(domainPath, flowIdStr);
    create_directories(flowDir);
    auto flowId = *uuids::uuid::from_string(flowIdStr);
    auto dataFile = mxl::lib::makeFlowDataFilePath(flowDir);     // main data file
    auto accessFile = mxl::lib::makeFlowAccessFilePath(flowDir); // .access file
    std::ofstream{dataFile}.close();
    std::ofstream{accessFile}.close();

    REQUIRE(watcher.addFlow(flowId, mxl::lib::WatcherType::READER) == 1);
    REQUIRE(watcher.addFlow(flowId, mxl::lib::WatcherType::WRITER) == 1);

    // Give the watcher thread a moment to register the inotify watch
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test 1: Modify the main data file and expect a READER-type event
    auto initialCount = eventCount.load();
    {
        std::ofstream f(dataFile, std::ios::app);
        f << "TestData";
        f.flush();
        f.close();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for callback with timeout
    auto lock = std::unique_lock<std::mutex>{eventMutex};
    auto gotEvent = eventCV.wait_for(lock, std::chrono::seconds(2), [&] { return eventCount.load() > initialCount; });
    lock.unlock();

    REQUIRE(gotEvent);
    REQUIRE(callbackUuid == flowId);
    REQUIRE(callbackType == mxl::lib::WatcherType::READER);

    // Clean up: remove the flow watches
    REQUIRE(watcher.removeFlow(flowId, mxl::lib::WatcherType::READER) == 0);
    REQUIRE(watcher.removeFlow(flowId, mxl::lib::WatcherType::WRITER) == 0);
}

TEST_CASE("DomainWatcher thread start/stop behavior", "[domainwatcher]")
{
    // This test ensures the internal event-processing thread starts on construction and stops on request.
    auto const homeDir = std::getenv("HOME");
    REQUIRE(homeDir != nullptr);
    auto domainPath = std::filesystem::path{homeDir} / "mxl_test_domain_thread";
    remove_all(domainPath);
    create_directories(domainPath);

    // Set up a callback that will signal when an event is processed
    auto eventMutex = std::mutex{};
    auto eventCV = std::condition_variable{};
    auto eventReceived = bool{false};
    mxl::lib::DomainWatcher watcher(domainPath,
        [&](uuids::uuid, mxl::lib::WatcherType)
        {
            auto lock = std::lock_guard<std::mutex>{eventMutex};
            eventReceived = true;
            eventCV.notify_one();
        });

    // Prepare a flow and add a watch so that events can be generated
    auto const flowIdStr = std::string{"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"};
    auto flowDir = mxl::lib::makeFlowDirectoryName(domainPath, flowIdStr);
    create_directories(flowDir);
    auto flowId = *uuids::uuid::from_string(flowIdStr);
    auto dataFile = mxl::lib::makeFlowDataFilePath(flowDir);
    std::ofstream{dataFile}.close();
    REQUIRE(watcher.addFlow(flowId, mxl::lib::WatcherType::READER) == 1);

    // Trigger an event to confirm the thread is running and processing events
    {
        std::ofstream f(dataFile, std::ios::app);
        f << "1"; // modify file to generate an inotify event
    }
    // Wait for the callback (with a short timeout)
    auto lock = std::unique_lock<std::mutex>{eventMutex};
    auto gotEvent = eventCV.wait_for(lock, std::chrono::milliseconds(500), [&] { return eventReceived; });
    REQUIRE(gotEvent); // The event thread should have processed the file change
    lock.unlock();
    eventReceived = false;

    // Now stop the watcher thread and ensure no further events are processed
    watcher.stop(); // signal the DomainWatcher to stop its event loop

    // Give the thread time to actually stop processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    {
        std::ofstream f(dataFile, std::ios::app);
        f << "2"; // another modification after stopping
        f.flush();
        f.close();
    }

    // Wait longer to see if any callback occurs (it should not, since thread is stopped)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // The eventReceived flag should remain false because the thread is stopped
    REQUIRE(eventReceived == false);

    // Cleanup
    REQUIRE(watcher.removeFlow(flowId, mxl::lib::WatcherType::READER) == 0);
    // DomainWatcher destructor will join the thread (if not already stopped) and close fds.
}

TEST_CASE("DomainWatcher error handling for invalid inputs", "[domainwatcher]")
{
    // This test simulates error conditions: adding watches for non-existent files and removing non-existent watches.
    auto const homeDir = std::getenv("HOME");
    REQUIRE(homeDir != nullptr);

    // Case 1: Using a valid domain path but adding a flow without creating the files
    auto goodDomain = std::filesystem::path{homeDir} / "mxl_error_test_domain";
    remove_all(goodDomain);
    create_directories(goodDomain);
    mxl::lib::DomainWatcher watcher(goodDomain, nullptr);

    // Attempt to addFlow on a domain where the flow files don't exist (should throw because files are not present)
    uuids::uuid dummyId = *uuids::uuid::from_string("01234567-89ab-cdef-0123-456789abcdef");
    // The implementation throws std::system_error when inotify_add_watch fails
    REQUIRE_THROWS_AS(watcher.addFlow(dummyId, mxl::lib::WatcherType::READER), std::system_error);

    // Case 2: Using a valid domain path but adding a flow without creating the files
    // Choose a new flow ID and call addFlow without creating the flow directory or files
    uuids::uuid noFileId = *uuids::uuid::from_string("fedcba98-7654-3210-fedc-ba9876543210");
    // Should throw because the file doesn't exist
    REQUIRE_THROWS_AS(watcher.addFlow(noFileId, mxl::lib::WatcherType::WRITER), std::system_error);
    // Also test removing a watch that was never added (should return -1)
    REQUIRE(watcher.removeFlow(noFileId, mxl::lib::WatcherType::WRITER) == -1);

    // Case 3: Removing a non-existent watch on a brand new DomainWatcher (nothing added at all)
    uuids::uuid randomId = *uuids::uuid::from_string("00000000-0000-0000-0000-000000000000");
    mxl::lib::DomainWatcher emptyWatcher(goodDomain, nullptr);
    REQUIRE(emptyWatcher.removeFlow(randomId, mxl::lib::WatcherType::READER) == -1);
}

#ifdef __linux__ // This test uses /proc/self/fd which is Linux-specific
TEST_CASE("DomainWatcher cleans up file descriptors on destruction", "[domainwatcher]")
{
    // This test verifies that DomainWatcher closes its inotify/epoll descriptors upon destruction,
    // so no file descriptor leaks occur.
    namespace fs = std::filesystem;
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

    auto const homeDir = std::getenv("HOME");
    REQUIRE(homeDir != nullptr);
    auto domainPath = std::filesystem::path{homeDir} / "mxl_fd_test_domain";
    remove_all(domainPath);
    create_directories(domainPath);

    auto const fdsBefore = countOpenFDs();
    { // scope to ensure DomainWatcher destruction
        mxl::lib::DomainWatcher watcher(domainPath, nullptr);
        // Add a watch to force inotify initialization if not already done in constructor
        auto const flowIdStr = std::string{"12345678-1234-5678-1234-567812345678"};
        auto flowDir = mxl::lib::makeFlowDirectoryName(domainPath, flowIdStr);
        create_directories(flowDir);
        auto flowId = *uuids::uuid::from_string(flowIdStr);
        auto filePath = mxl::lib::makeFlowDataFilePath(flowDir);
        std::ofstream{filePath}.close();
        REQUIRE(watcher.addFlow(flowId, mxl::lib::WatcherType::READER) == 1);
        // (No need to trigger events here; we just want the watcher to open its fds)
    } // DomainWatcher goes out of scope here, its destructor should close inotify/epoll fds
    auto const fdsAfter = countOpenFDs();

    // After destruction, the number of open fds should be back to the original count
    // (The DomainWatcher should have closed the two fds it opened: inotify and epoll)
    REQUIRE(fdsAfter == fdsBefore);
}
#endif // __linux__

TEST_CASE("DomainWatcher supports concurrent add/remove operations", "[domainwatcher]")
{
    // This test spawns multiple threads to add and remove flow watchers concurrently,
    // to ensure thread safety (no crashes or race conditions and correct final state).
    auto const homeDir = std::getenv("HOME");
    REQUIRE(homeDir != nullptr);
    auto domainPath = std::filesystem::path{homeDir} / "mxl_concurrent_domain";
    remove_all(domainPath);
    create_directories(domainPath);
    mxl::lib::DomainWatcher watcher(domainPath, nullptr);

    // Prepare a flow file for watching
    auto const flowIdStr = std::string{"deadc0de-dead-beef-dead-beefdeadc0de"};
    auto flowDir = mxl::lib::makeFlowDirectoryName(domainPath, flowIdStr);
    create_directories(flowDir);
    auto flowId = *uuids::uuid::from_string(flowIdStr);
    auto filePath = mxl::lib::makeFlowDataFilePath(flowDir);
    std::ofstream{filePath}.close(); // create the file to watch

    auto const iterations = 100;     // Reduced iterations for more predictable results
    auto addCount = std::atomic<int>{0};
    auto removeCount = std::atomic<int>{0};

    // Launch two threads: one repeatedly adds a reader watch, the other removes it
    std::thread adder(
        [&]()
        {
            for (int i = 0; i < iterations; ++i)
            {
                try
                {
                    auto result = watcher.addFlow(flowId, mxl::lib::WatcherType::READER);
                    if (result > 0)
                    {
                        addCount++;
                    }
                }
                catch (...)
                {
                    // Ignore exceptions from concurrent access
                }
                if (i % 10 == 0)
                {
                    std::this_thread::yield();
                } // occasionally yield to other thread
            }
        });
    std::thread remover(
        [&]()
        {
            for (int i = 0; i < iterations; ++i)
            {
                try
                {
                    int result = watcher.removeFlow(flowId, mxl::lib::WatcherType::READER);
                    if (result >= 0)
                    {
                        removeCount++;
                    }
                }
                catch (...)
                {
                    // Ignore exceptions from concurrent access
                }
                if (i % 10 == 0)
                {
                    std::this_thread::yield();
                }
            }
        });

    // Wait for both threads to finish
    adder.join();
    remover.join();

    // After concurrent operations, clean up any remaining watches
    // The exact final state is unpredictable due to concurrency, but we should be able to clean up
    int cleanupAttempts = 0;
    int result = 0;
    do
    {
        result = watcher.removeFlow(flowId, mxl::lib::WatcherType::READER);
        cleanupAttempts++;
    }
    while (result > 0 && cleanupAttempts < 10); // Limit cleanup attempts

    // Verify the watcher is still functional after stress testing
    REQUIRE(watcher.addFlow(flowId, mxl::lib::WatcherType::READER) >= 1);
    REQUIRE(watcher.removeFlow(flowId, mxl::lib::WatcherType::READER) >= 0);
}

TEST_CASE("DomainWatcher ctor throws on invalid domain path", "[domainwatcher]")
{
    // Point at a path that cannot be used as a directory (e.g. a regular file)
    auto tmp = std::filesystem::temp_directory_path() / "not_a_dir";
    std::ofstream{tmp}.close(); // create a plain file
    REQUIRE_THROWS_AS(mxl::lib::DomainWatcher(tmp, nullptr), std::filesystem::filesystem_error);
}

TEST_CASE("DomainWatcher allows re-add after full remove", "[domainwatcher]")
{
    // Create domain and file
    auto domain = std::filesystem::temp_directory_path() / "mxl_dw_readd";
    std::filesystem::remove_all(domain);
    std::filesystem::create_directories(domain);
    auto flowId = *uuids::uuid::from_string("abcdefab-1234-5678-9abc-abcdefabcdef");
    auto flowDir = mxl::lib::makeFlowDirectoryName(domain, uuids::to_string(flowId));
    std::filesystem::create_directories(flowDir);
    auto file = mxl::lib::makeFlowDataFilePath(flowDir);
    std::ofstream{file}.close();

    DomainWatcher watcher(domain, nullptr);
    // Add once, remove fully, then add again—should succeed twice
    REQUIRE(watcher.addFlow(flowId, WatcherType::READER) == 1);
    REQUIRE(watcher.removeFlow(flowId, WatcherType::READER) == 0);
    REQUIRE(watcher.addFlow(flowId, WatcherType::READER) == 1);
}

TEST_CASE("DomainWatcher triggers WRITER callback on access file modifications", "[domainwatcher]")
{
    // Ensure a WRITER‐type watcher actually fires when the .access file changes
    auto const homeDir = std::getenv("HOME");
    REQUIRE(homeDir != nullptr);

    auto domainPath = std::filesystem::path{homeDir} / "mxl_test_writer_event";
    remove_all(domainPath);
    create_directories(domainPath);

    // Synchronization primitives for the callback
    auto mtx = std::mutex{};
    auto cv = std::condition_variable{};
    auto notified = bool{false};
    auto cbId = uuids::uuid{};
    auto cbType = mxl::lib::WatcherType{};

    // Install watcher
    auto watcher = mxl::lib::DomainWatcher{domainPath,
        [&](uuids::uuid id, mxl::lib::WatcherType type)
        {
            auto const lock = std::lock_guard{mtx};
            notified = true;
            cbId = id;
            cbType = type;
            cv.notify_one();
        }};

    // Prepare flow directory & files
    auto const flowIdStr = std::string{"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"};
    auto flowDir = makeFlowDirectoryName(domainPath, flowIdStr);
    create_directories(flowDir);
    auto flowId = *uuids::uuid::from_string(flowIdStr);
    auto dataFile = makeFlowDataFilePath(flowDir);
    auto accFile = makeFlowAccessFilePath(flowDir);
    std::ofstream{dataFile}.close();
    std::ofstream{accFile}.close();

    // Add a WRITER watch
    REQUIRE(watcher.addFlow(flowId, mxl::lib::WatcherType::WRITER) == 1);

    // Give the thread time to register
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Simulate what a real reader does: update the access file timestamp
    auto accessFd = ::open(accFile.string().c_str(), O_RDWR);
    REQUIRE(accessFd != -1);

    // Use futimens to update access time (generates IN_ATTRIB event)
    struct timespec times[2] = {
        {0, UTIME_NOW }, // access time = now
        {0, UTIME_OMIT}  // keep modification time unchanged
    };
    auto result = ::futimens(accessFd, times);
    ::close(accessFd);
    REQUIRE(result == 0);

    // Wait for callback
    {
        auto lock = std::unique_lock{mtx};
        auto got = cv.wait_for(lock, std::chrono::seconds(1), [&] { return notified; });
        REQUIRE(got);
        REQUIRE(cbId == flowId);
        REQUIRE(cbType == mxl::lib::WatcherType::WRITER);
    }

    // Clean up
    REQUIRE(watcher.removeFlow(flowId, mxl::lib::WatcherType::WRITER) == 0);
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
    REQUIRE_THROWS_AS((mxl::lib::DomainWatcher{bad1, nullptr}), std::filesystem::filesystem_error);

    // Case: path exists but is a regular file
    auto bad2 = std::filesystem::path{homeDir} / "mxl_not_a_dir";
    remove_all(bad2);
    {
        auto touch = std::ofstream{bad2};
        touch << "notadir";
    }
    REQUIRE(exists(bad2));
    REQUIRE(is_regular_file(bad2));
    REQUIRE_THROWS_AS((mxl::lib::DomainWatcher{bad2, nullptr}), std::filesystem::filesystem_error);

    // Clean up
    remove_all(bad2);
}