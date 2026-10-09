// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <sys/mman.h>
#include <catch2/catch_test_macros.hpp>
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

TEST_CASE("Fixed mapping enforces capacity", "[shared mem]")
{
    auto const path = std::filesystem::path{"./mxl_shmtest_capacity.bin"};
    remove(path);

    {
        auto writer = mxl::lib::SharedMemoryInstance<Payload>{
            path.string().c_str(), mxl::lib::AccessMode::CREATE_READ_WRITE, EXTRA_SIZE, mxl::lib::LockMode::Shared};
        REQUIRE(writer.mappedSize() == TOTAL_SIZE);
    }

    auto* const reservation = ::mmap(nullptr, TOTAL_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    REQUIRE(reservation != MAP_FAILED);

    REQUIRE_THROWS(mxl::lib::SharedMemoryInstance<Payload>{
        path.string().c_str(), mxl::lib::AccessMode::READ_ONLY, 0U, mxl::lib::LockMode::Shared, reservation, TOTAL_SIZE - 1U});

    REQUIRE(::munmap(reservation, TOTAL_SIZE) == 0);
    remove(path);
}

TEST_CASE("Shared memory instance rejects size overflow", "[shared mem]")
{
    REQUIRE_THROWS_AS((mxl::lib::SharedMemoryInstance<Payload>{
                          "unused", mxl::lib::AccessMode::READ_ONLY, std::numeric_limits<std::size_t>::max(), mxl::lib::LockMode::None}),
        std::overflow_error);
}
