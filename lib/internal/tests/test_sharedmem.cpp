// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/SharedMemory.hpp"

namespace
{
    struct Payload
    {
        std::size_t testData;

        constexpr Payload() noexcept
            : testData{123456789ULL}
        {}
    };

    constexpr auto const TOTAL_SIZE = 4096ULL;
    constexpr auto const EXTRA_SIZE = TOTAL_SIZE - sizeof(Payload);
}

TEST_CASE("Create", "[shared mem]")
{
    auto const p = std::filesystem::path{"./mxl_shmtest_file.bin"};
    remove(p);

    auto reader = mxl::lib::SharedMemoryInstance<Payload>{};

    REQUIRE(!reader);
    REQUIRE(!reader.isValid());
    REQUIRE(reader.get() == nullptr);
    REQUIRE(reader.mappedSize() == 0U);
    REQUIRE(!reader.created());

    // Try to open a non-existing file for reading without automatically creating it.
    REQUIRE_THROWS(
        [&]()
        {
            reader = mxl::lib::SharedMemoryInstance<Payload>{p.string().c_str(), mxl::lib::AccessMode::READ_ONLY, 0, mxl::lib::LockMode::Shared};
        }());

    REQUIRE(!reader);
    REQUIRE(!reader.isValid());
    REQUIRE(reader.get() == nullptr);
    REQUIRE(reader.mappedSize() == 0U);
    REQUIRE(!reader.created());

    auto writer = mxl::lib::SharedMemoryInstance<Payload>{};

    REQUIRE(!writer);
    REQUIRE(!writer.isValid());
    REQUIRE(writer.get() == nullptr);
    REQUIRE(writer.mappedSize() == 0U);
    REQUIRE(!writer.created());

    // Try to open a non-existing file for wrting without automatically creating it.
    REQUIRE_THROWS(
        [&]()
        {
            writer = mxl::lib::SharedMemoryInstance<Payload>{p.string().c_str(), mxl::lib::AccessMode::READ_WRITE, 0, mxl::lib::LockMode::Shared};
        }());

    REQUIRE(!writer);
    REQUIRE(!writer.isValid());
    REQUIRE(writer.get() == nullptr);
    REQUIRE(writer.mappedSize() == 0U);
    REQUIRE(!writer.created());

    // Now try to open it with the create mode set to true
    REQUIRE_NOTHROW(
        [&]()
        {
            writer = mxl::lib::SharedMemoryInstance<Payload>{
                p.string().c_str(), mxl::lib::AccessMode::CREATE_READ_WRITE, EXTRA_SIZE, mxl::lib::LockMode::Shared};
        }());

    // Confirm that the mapping is valid
    REQUIRE(!!writer);
    REQUIRE(writer.isValid());
    REQUIRE(writer.get() != nullptr);
    REQUIRE(writer.mappedSize() == TOTAL_SIZE);
    REQUIRE(writer.created());

    // Confirm correct creatiion via default constructor of payload
    REQUIRE(writer.get()->testData == 123456789ULL);

    // Confirm that it exists.
    REQUIRE(exists(p));
    // Confirm that the file is the right size.
    REQUIRE(file_size(p) == TOTAL_SIZE);

    // Try to open again the existing file.
    REQUIRE_NOTHROW(
        [&]()
        {
            reader = mxl::lib::SharedMemoryInstance<Payload>{p.string().c_str(), mxl::lib::AccessMode::READ_ONLY, 0, mxl::lib::LockMode::Shared};
        }());

    // Confirm that the mapping is valid
    REQUIRE(!!reader);
    REQUIRE(reader.isValid());
    REQUIRE(reader.get() != nullptr);
    REQUIRE(reader.mappedSize() == TOTAL_SIZE);
    REQUIRE(!reader.created());

    auto const writer_data = writer.get();
    auto const reader_data = reader.get();

    writer_data->testData = 99;

    // Confirm that the data is shared between the two objects.
    REQUIRE(reader_data->testData == 99);

    REQUIRE_NOTHROW(
        [&]()
        {
            reader = {};
            writer = {};
        }());

    REQUIRE(!reader);
    REQUIRE(!reader.isValid());
    REQUIRE(reader.get() == nullptr);
    REQUIRE(reader.mappedSize() == 0U);
    REQUIRE(!reader.created());

    REQUIRE(!writer);
    REQUIRE(!writer.isValid());
    REQUIRE(writer.get() == nullptr);
    REQUIRE(writer.mappedSize() == 0U);
    REQUIRE(!writer.created());

    // Final cleanup
    if (exists(p))
    {
        remove(p);
    }
}

#ifdef _WIN32
// On Windows the data of a segment lives in a named in-memory section. The file only anchors the segment.
TEST_CASE("Windows: data lives in memory, not in the file", "[shared mem][windows]")
{
    auto const p = std::filesystem::path{"./mxl_shmtest_memory.bin"};
    std::filesystem::remove(p);

    auto writer = mxl::lib::SharedMemoryInstance<Payload>{
        p.string().c_str(), mxl::lib::AccessMode::CREATE_READ_WRITE, TOTAL_SIZE, mxl::lib::LockMode::Shared};
    REQUIRE(writer);
    REQUIRE(writer.mappedSize() >= TOTAL_SIZE);
    writer.get()->testData = 0xC0FFEEULL;

    // The file has the right size, but the data never reaches it.
    REQUIRE(std::filesystem::file_size(p) == writer.mappedSize());
    {
        auto file = std::ifstream{p, std::ios::binary};
        auto value = std::size_t{1};
        file.read(reinterpret_cast<char*>(&value), sizeof value);
        REQUIRE(value == 0U);
    }

    // Another mapping of the same segment sees the data.
    {
        auto reader = mxl::lib::SharedMemoryInstance<Payload>{p.string().c_str(), mxl::lib::AccessMode::READ_ONLY, 0, mxl::lib::LockMode::None};
        REQUIRE(reader);
        REQUIRE(reader.get()->testData == 0xC0FFEEULL);
        REQUIRE(reader.mappedSize() == std::filesystem::file_size(p));
    }

    // The data is gone with the last handle, while the anchor file stays: the segment is stale.
    writer = mxl::lib::SharedMemoryInstance<Payload>{};
    REQUIRE(std::filesystem::exists(p));
    REQUIRE_THROWS_AS(mxl::lib::SharedMemoryInstance<Payload>(p.string().c_str(), mxl::lib::AccessMode::READ_ONLY, 0, mxl::lib::LockMode::None),
        mxl::lib::SectionMissingError);

    std::filesystem::remove(p);
}

TEST_CASE("Windows: a pinned section survives its segment", "[shared mem][windows]")
{
    auto const p = std::filesystem::path{"./mxl_shmtest_pin.bin"};
    std::filesystem::remove(p);

    auto pin = mxl::lib::SectionPin{};
    {
        auto writer = mxl::lib::SharedMemoryInstance<Payload>{
            p.string().c_str(), mxl::lib::AccessMode::CREATE_READ_WRITE, TOTAL_SIZE, mxl::lib::LockMode::Shared};
        writer.get()->testData = 42U;
        pin = writer.pinSection();
    }

    // Closed and reopened while pinned (this is what publishing a flow directory does).
    {
        auto reopened = mxl::lib::SharedMemoryInstance<Payload>{p.string().c_str(), mxl::lib::AccessMode::READ_WRITE, 0, mxl::lib::LockMode::Shared};
        REQUIRE(reopened.get()->testData == 42U);
    }

    pin = mxl::lib::SectionPin{};
    REQUIRE_THROWS_AS(mxl::lib::SharedMemoryInstance<Payload>(p.string().c_str(), mxl::lib::AccessMode::READ_ONLY, 0, mxl::lib::LockMode::None),
        mxl::lib::SectionMissingError);

    std::filesystem::remove(p);
}

// FlowManager uses the identity in the error to lock the stale file, see StaleFileLock.
TEST_CASE("Windows: a missing section reports the identity of its file", "[shared mem][windows]")
{
    auto const p = std::filesystem::path{"./mxl_shmtest_identity.bin"};
    std::filesystem::remove(p);
    {
        auto writer = mxl::lib::SharedMemoryInstance<Payload>{
            p.string().c_str(), mxl::lib::AccessMode::CREATE_READ_WRITE, TOTAL_SIZE, mxl::lib::LockMode::Shared};
    }

    try
    {
        auto const reader = mxl::lib::SharedMemoryInstance<Payload>{p.string().c_str(), mxl::lib::AccessMode::READ_ONLY, 0, mxl::lib::LockMode::None};
        FAIL("The section of the stale file still exists.");
    }
    catch (mxl::lib::SectionMissingError const& e)
    {
        REQUIRE(e.fileIdentity() == mxl::lib::fileIdentity(p));
    }

    std::filesystem::remove(p);
}
#endif

// Two processes that replace the same stale flow both try to lock its data file. The one that waited must not get the lock on the file
// that the other one created in its place.
TEST_CASE("A stale file lock does not lock a replacement file", "[shared mem]")
{
    auto const p = std::filesystem::path{"./mxl_shmtest_stale.bin"};
    std::filesystem::remove(p);
    {
        auto writer = mxl::lib::SharedMemoryInstance<Payload>{
            p.string().c_str(), mxl::lib::AccessMode::CREATE_READ_WRITE, TOTAL_SIZE, mxl::lib::LockMode::Shared};
    }
    auto const staleIdentity = mxl::lib::fileIdentity(p);
    REQUIRE(staleIdentity);

    auto replacement = mxl::lib::SharedMemoryInstance<Payload>{};
    {
        auto lock = std::make_unique<mxl::lib::StaleFileLock>(p.string().c_str(), *staleIdentity);
        REQUIRE(*lock);

        // The second replacer waits for the lock, or opens the path after the replacement exists. Either way it must not get the lock.
        auto waiter = std::async(std::launch::async,
            [&]()
            {
                auto const other = mxl::lib::StaleFileLock{p.string().c_str(), *staleIdentity};
                return static_cast<bool>(other);
            });
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        // Delete the stale file while holding the lock and create a new segment in its place, as replacing a stale flow does.
        std::filesystem::remove(p);
        replacement = mxl::lib::SharedMemoryInstance<Payload>{
            p.string().c_str(), mxl::lib::AccessMode::CREATE_READ_WRITE, TOTAL_SIZE, mxl::lib::LockMode::Shared};
        replacement.get()->testData = 7U;
        lock.reset();

        REQUIRE_FALSE(waiter.get());
    }

    // The replacement is untouched.
    REQUIRE(std::filesystem::exists(p));
    REQUIRE(replacement.get()->testData == 7U);
    replacement = mxl::lib::SharedMemoryInstance<Payload>{};
    std::filesystem::remove(p);
}
