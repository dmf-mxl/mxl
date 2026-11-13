// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <catch2/catch_test_macros.hpp>
#include <mxl/dataformat.h>
#include <mxl/flow.h>
#include <mxl/flowinfo.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "Latency.hpp"

namespace
{
    /// Video flow at 30000/1001 from the library test data. The default 200 ms history gives 5 grains.
    constexpr auto videoFlowFile = MXL_INFO_TEST_DATA_DIR "/v210_flow.json";
    /// Id of the flow in videoFlowFile.
    constexpr auto videoFlowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    /// Audio flow at 48 kHz from the library test data. The default 200 ms history gives 19456 samples.
    constexpr auto audioFlowFile = MXL_INFO_TEST_DATA_DIR "/audio_flow.json";
    /// Id of the flow in audioFlowFile.
    constexpr auto audioFlowId = "b3bb5be7-9fe9-4324-a5bb-4c70e1084449";
    /// Samples per audio commit, 10 ms at 48 kHz.
    constexpr auto audioBatch = std::uint64_t{480};

    /**
     * @brief Read a whole file into a string.
     * @param[in] path File to read.
     * @return The file contents.
     * @throws std::runtime_error if the file cannot be opened.
     */
    std::string readFile(std::filesystem::path const& path)
    {
        auto file = std::ifstream{path};
        if (!file)
        {
            throw std::runtime_error{"Cannot open " + path.string()};
        }
        auto contents = std::ostringstream{};
        contents << file.rdbuf();
        return contents.str();
    }

    /// Temporary MXL domain directory, removed with its contents on destruction.
    class TempDomain
    {
    public:
        /// Create a new empty directory. It is in /dev/shm on Linux and in /tmp on macOS, as in the library tests.
        TempDomain()
        {
#ifdef __APPLE__
            char path[] = "/tmp/mxl_info_test_XXXXXX";
#else
            char path[] = "/dev/shm/mxl_info_test_XXXXXX";
#endif
            if (::mkdtemp(path) == nullptr)
            {
                throw std::runtime_error{"Cannot create a temporary domain."};
            }
            _path = path;
        }

        TempDomain(TempDomain&&) = delete;
        TempDomain(TempDomain const&) = delete;
        TempDomain& operator=(TempDomain&&) = delete;
        TempDomain& operator=(TempDomain const&) = delete;

        /// Remove the directory. Errors are ignored, since a destructor cannot report them.
        ~TempDomain()
        {
            auto error = std::error_code{};
            std::filesystem::remove_all(_path, error);
        }

        /// @return The directory path.
        [[nodiscard]]
        std::filesystem::path const& path() const noexcept
        {
            return _path;
        }

    private:
        std::filesystem::path _path; ///< Directory created by the constructor.
    };

    /// One flow in a temporary domain, with a writer and a reader on the same instance.
    class FlowFixture
    {
    public:
        /**
         * @brief Create the domain, the instance, the flow writer and a reader.
         * @param[in] flowFile NMOS flow definition to create the flow from.
         * @param[in] flowId Id of the flow in @p flowFile.
         * @throws std::runtime_error if a step fails. Members already created are cleaned up by their destructors.
         */
        FlowFixture(std::filesystem::path const& flowFile, char const* flowId)
            : _domain{}
            , _instance{::mxlCreateInstance(_domain.path().c_str(), ""), &::mxlDestroyInstance}
            , _writer{nullptr}
            , _reader{nullptr}
        {
            if (!_instance)
            {
                throw std::runtime_error{"Cannot create an MXL instance."};
            }
            auto const flowDef = readFile(flowFile);
            auto configInfo = ::mxlFlowConfigInfo{};
            auto created = false;
            if (::mxlCreateFlowWriter(_instance.get(), flowDef.c_str(), "", &_writer, &configInfo, &created) != MXL_STATUS_OK)
            {
                throw std::runtime_error{"Cannot create the flow writer."};
            }
            if (::mxlCreateFlowReader(_instance.get(), flowId, "", &_reader) != MXL_STATUS_OK)
            {
                throw std::runtime_error{"Cannot create the flow reader."};
            }
        }

        FlowFixture(FlowFixture&&) = delete;
        FlowFixture(FlowFixture const&) = delete;
        FlowFixture& operator=(FlowFixture&&) = delete;
        FlowFixture& operator=(FlowFixture const&) = delete;

        /// Release the reader, then the writer. The instance and the domain are removed after that by their members.
        ~FlowFixture()
        {
            ::mxlReleaseFlowReader(_instance.get(), _reader);
            ::mxlReleaseFlowWriter(_instance.get(), _writer);
        }

        /// @return The reader on the flow.
        [[nodiscard]]
        ::mxlFlowReader reader() const noexcept
        {
            return _reader;
        }

        /**
         * @brief Read the flow info through the reader.
         * @return The flow info.
         * @throws std::runtime_error if the read fails.
         */
        [[nodiscard]]
        ::mxlFlowInfo info() const
        {
            auto info = ::mxlFlowInfo{};
            if (::mxlFlowReaderGetInfo(_reader, &info) != MXL_STATUS_OK)
            {
                throw std::runtime_error{"Cannot read the flow info."};
            }
            return info;
        }

        /**
         * @brief Write and commit a complete grain of a discrete flow.
         * @param[in] index Grain index. It must be after the last committed index.
         * @return The status of the first call that fails, or MXL_STATUS_OK.
         */
        ::mxlStatus commitGrain(std::uint64_t index) const noexcept
        {
            auto grainInfo = ::mxlGrainInfo{};
            auto* payload = static_cast<std::uint8_t*>(nullptr);
            if (auto const status = ::mxlFlowWriterOpenGrain(_writer, index, &grainInfo, &payload); status != MXL_STATUS_OK)
            {
                return status;
            }
            // Opening resets validSlices. A reader waits for the full grain, so mark every slice as written.
            grainInfo.validSlices = grainInfo.totalSlices;
            return ::mxlFlowWriterCommitGrain(_writer, &grainInfo);
        }

        /**
         * @brief Write and commit samples of a continuous flow.
         * @param[in] index Index of the last sample. It becomes the head index.
         * @param[in] count Number of samples, ending at @p index.
         * @return The status of the first call that fails, or MXL_STATUS_OK.
         */
        ::mxlStatus commitSamples(std::uint64_t index, std::uint64_t count) const noexcept
        {
            auto slices = ::mxlMutableWrappedMultiBufferSlice{};
            if (auto const status = ::mxlFlowWriterOpenSamples(_writer, index, count, &slices); status != MXL_STATUS_OK)
            {
                return status;
            }
            return ::mxlFlowWriterCommitSamples(_writer);
        }

    private:
        TempDomain _domain;                                                          ///< Domain directory. Declared first so it is removed last.
        std::unique_ptr<::mxlInstance_t, decltype(&::mxlDestroyInstance)> _instance; ///< Instance for writer and reader.
        ::mxlFlowWriter _writer;                                                     ///< Writer that creates the flow.
        ::mxlFlowReader _reader;                                                     ///< Reader passed to waitForNextCommit.
    };

    /// Time to wait before the writer thread commits. Well below the shortest timeout used here (133 ms).
    constexpr auto commitDelay = std::chrono::milliseconds{20};
}

TEST_CASE("Next commit: a video commit ends the wait", "[mxl-info][next-commit]")
{
    auto const flow = FlowFixture{videoFlowFile, videoFlowId};
    auto const rate = flow.info().config.common.grainRate;
    auto const head = ::mxlGetCurrentIndex(&rate);
    REQUIRE(flow.commitGrain(head) == MXL_STATUS_OK);
    auto const before = flow.info();
    REQUIRE(before.runtime.headIndex == head);

    auto commitStatus = std::atomic<::mxlStatus>{MXL_ERR_UNKNOWN};
    auto commitTimeNs = std::atomic<std::uint64_t>{0};
    auto writer = std::thread{[&]()
        {
            std::this_thread::sleep_for(commitDelay);
            commitTimeNs = ::mxlGetTime();
            commitStatus = flow.commitGrain(head + 1U);
        }};
    auto const sample = mxl::info::waitForNextCommit(flow.reader(), before);
    writer.join();

    REQUIRE(commitStatus == MXL_STATUS_OK);
    if (!sample.has_value())
    {
        FAIL("waitForNextCommit returned no sample.");
        return;
    }
    CHECK(sample->runtime.headIndex == head + 1U);
    // The time is taken after the wait returns, so it is not before the commit.
    CHECK(sample->nowNs >= commitTimeNs);
}

TEST_CASE("Next commit: an audio commit ends the wait", "[mxl-info][next-commit]")
{
    auto const flow = FlowFixture{audioFlowFile, audioFlowId};
    auto const rate = flow.info().config.common.grainRate;
    auto const head = ::mxlGetCurrentIndex(&rate);
    REQUIRE(flow.commitSamples(head, audioBatch) == MXL_STATUS_OK);
    auto const before = flow.info();
    REQUIRE(before.runtime.headIndex == head);

    auto commitStatus = std::atomic<::mxlStatus>{MXL_ERR_UNKNOWN};
    auto writer = std::thread{[&]()
        {
            std::this_thread::sleep_for(commitDelay);
            commitStatus = flow.commitSamples(head + audioBatch, audioBatch);
        }};
    auto const sample = mxl::info::waitForNextCommit(flow.reader(), before);
    writer.join();

    REQUIRE(commitStatus == MXL_STATUS_OK);
    if (!sample.has_value())
    {
        FAIL("waitForNextCommit returned no sample.");
        return;
    }
    // The wait asks for one sample after the head, and a commit adds a whole batch.
    CHECK(sample->runtime.headIndex == head + audioBatch);
}

TEST_CASE("Next commit: no commit ends the wait after one history duration", "[mxl-info][next-commit]")
{
    auto const flow = FlowFixture{videoFlowFile, videoFlowId};
    auto const rate = flow.info().config.common.grainRate;
    auto const head = ::mxlGetCurrentIndex(&rate);
    REQUIRE(flow.commitGrain(head) == MXL_STATUS_OK);
    auto const before = flow.info();

    auto const start = std::chrono::steady_clock::now();
    auto const sample = mxl::info::waitForNextCommit(flow.reader(), before);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    CHECK_FALSE(sample.has_value());
    // The history is 4 grains at 30000/1001, which is 133.47 ms. The lower bound allows for clock rounding.
    CHECK(elapsed >= std::chrono::milliseconds{130});
    CHECK(elapsed < std::chrono::seconds{1});
}

TEST_CASE("Next commit: no audio commit ends the wait after one history duration", "[mxl-info][next-commit]")
{
    auto const flow = FlowFixture{audioFlowFile, audioFlowId};
    auto const rate = flow.info().config.common.grainRate;
    auto const head = ::mxlGetCurrentIndex(&rate);
    REQUIRE(flow.commitSamples(head, audioBatch) == MXL_STATUS_OK);
    auto const before = flow.info();

    auto const start = std::chrono::steady_clock::now();
    auto const sample = mxl::info::waitForNextCommit(flow.reader(), before);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    CHECK_FALSE(sample.has_value());
    // The history is 9728 samples at 48 kHz, which is 202.67 ms. The lower bound allows for clock rounding.
    CHECK(elapsed >= std::chrono::milliseconds{200});
    CHECK(elapsed < std::chrono::seconds{1});
}

TEST_CASE("Next commit: a writer jump past the ring counts as a commit", "[mxl-info][next-commit]")
{
    auto const flow = FlowFixture{videoFlowFile, videoFlowId};
    auto const rate = flow.info().config.common.grainRate;
    auto const head = ::mxlGetCurrentIndex(&rate);
    REQUIRE(flow.commitGrain(head) == MXL_STATUS_OK);
    auto const before = flow.info();

    // 100 grains is more than the 5-grain ring, so head + 1 is no longer readable and the reader reports TOO_LATE.
    REQUIRE(flow.commitGrain(head + 100U) == MXL_STATUS_OK);
    auto const sample = mxl::info::waitForNextCommit(flow.reader(), before);

    if (!sample.has_value())
    {
        FAIL("waitForNextCommit returned no sample.");
        return;
    }
    CHECK(sample->runtime.headIndex == head + 100U);
}

TEST_CASE("Next commit: an audio writer jump past the history counts as a commit", "[mxl-info][next-commit]")
{
    auto const flow = FlowFixture{audioFlowFile, audioFlowId};
    auto const rate = flow.info().config.common.grainRate;
    auto const head = ::mxlGetCurrentIndex(&rate);
    REQUIRE(flow.commitSamples(head, audioBatch) == MXL_STATUS_OK);
    auto const before = flow.info();

    // A jump of one ring length leaves head + 1 more than half the buffer behind the new head, so the reader
    // reports TOO_LATE.
    auto const jump = mxl::info::ringLength(before);
    REQUIRE(flow.commitSamples(head + jump, audioBatch) == MXL_STATUS_OK);
    auto const sample = mxl::info::waitForNextCommit(flow.reader(), before);

    if (!sample.has_value())
    {
        FAIL("waitForNextCommit returned no sample.");
        return;
    }
    CHECK(sample->runtime.headIndex == head + jump);
}

TEST_CASE("Next commit: event flows are not waited on", "[mxl-info][next-commit]")
{
    auto info = ::mxlFlowInfo{};
    info.config.common.format = MXL_DATA_FORMAT_EVENT;
    // The function returns before it uses the reader, so no flow is needed.
    CHECK_FALSE(mxl::info::waitForNextCommit(nullptr, info).has_value());
}
