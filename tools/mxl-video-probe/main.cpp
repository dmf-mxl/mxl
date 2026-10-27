// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Verify video grains and report delivery rate, latency and jitter.
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <ada.h>
#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <mxl-tools/Status.hpp>
#include <mxl/flow.h>
#include <mxl/flowinfo.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

namespace
{
    using mxl::tools::statusToString;

    /// Own an MXL instance until the enclosing scope exits.
    class ScopedMxlInstance
    {
    public:
        /// Open an instance for a domain.
        /// \param domain Directory containing the MXL domain.
        /// \throws std::runtime_error If the instance cannot be created.
        explicit ScopedMxlInstance(std::string const& domain)
            : _instance{::mxlCreateInstance(domain.c_str(), "")}
        {
            if (_instance == nullptr)
            {
                throw std::runtime_error{"Failed to create MXL instance."};
            }
        }

        /// Destroy the instance after its readers have been released.
        ~ScopedMxlInstance()
        {
            (void)::mxlDestroyInstance(_instance);
        }

        /// Copying an owned instance is disabled.
        ScopedMxlInstance(ScopedMxlInstance const&) = delete;
        /// Assigning an owned instance is disabled.
        ScopedMxlInstance& operator=(ScopedMxlInstance const&) = delete;

        /// Borrow the instance for SDK calls.
        /// \return A handle valid until this owner is destroyed.
        [[nodiscard]]
        constexpr ::mxlInstance get() const noexcept
        {
            return _instance;
        }

    private:
        ::mxlInstance _instance; ///< Owned instance handle.
    };

    /// Own a flow reader whose instance must outlive it.
    class ScopedFlowReader
    {
    public:
        /// Open a reader for a flow.
        /// \param instance Borrowed instance that must outlive this reader.
        /// \param flowId Identifier of the flow to open.
        /// \throws std::runtime_error If the reader cannot be created.
        ScopedFlowReader(::mxlInstance instance, std::string const& flowId)
            : _instance{instance}
            , _reader{nullptr}
        {
            auto const status = ::mxlCreateFlowReader(instance, flowId.c_str(), "", &_reader);
            if (status != MXL_STATUS_OK)
            {
                throw std::runtime_error{fmt::format("Failed to create flow reader: {}", statusToString(status))};
            }
        }

        /// Release the reader before its instance is destroyed.
        ~ScopedFlowReader()
        {
            (void)::mxlReleaseFlowReader(_instance, _reader);
        }

        /// Copying an owned reader is disabled.
        ScopedFlowReader(ScopedFlowReader const&) = delete;
        /// Assigning an owned reader is disabled.
        ScopedFlowReader& operator=(ScopedFlowReader const&) = delete;

        /// Borrow the reader for SDK calls.
        /// \return A handle valid until this owner is destroyed.
        [[nodiscard]]
        ::mxlFlowReader get() const noexcept
        {
            return _reader;
        }

    private:
        ::mxlInstance _instance; ///< Borrowed instance used to release the reader.
        ::mxlFlowReader _reader; ///< Owned reader handle.
    };

    /// Compare complete video grains with reference frames and report delivery rate, latency and jitter.
    /// \param domain Directory containing the MXL domain.
    /// \param flowId Flow identifier to read.
    /// \param referencePath Raw file containing a repeating sequence of complete frames.
    /// \param count Number of frames to read, at least 60.
    /// \param timeoutNs Maximum wait per grain in nanoseconds.
    /// \throws std::runtime_error If the reference, flow, read or timestamp conversion is invalid.
    /// \return EXIT_SUCCESS if bytes, order, grain indices and measured rate pass; EXIT_FAILURE otherwise.
    int verifyVideoFlow(std::string const& domain, std::string const& flowId, std::string const& referencePath, std::uint64_t count,
        std::uint64_t timeoutNs)
    {
        auto file = std::ifstream{referencePath, std::ios::binary | std::ios::ate};
        if (!file)
        {
            throw std::runtime_error{"Failed to open reference frame file."};
        }
        auto const fileSize = static_cast<std::streamoff>(file.tellg());
        if ((fileSize <= 0) || (fileSize > std::numeric_limits<std::streamsize>::max()))
        {
            throw std::runtime_error{"Reference file must contain complete frames."};
        }
        auto reference = std::vector<std::uint8_t>(static_cast<std::size_t>(fileSize));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(reference.data()), static_cast<std::streamsize>(reference.size())))
        {
            throw std::runtime_error{"Failed to read reference frame file."};
        }
        auto instance = ScopedMxlInstance{domain};
        auto reader = ScopedFlowReader{instance.get(), flowId};

        auto info = ::mxlFlowInfo{};
        if (auto const status = ::mxlFlowReaderGetInfo(reader.get(), &info); status != MXL_STATUS_OK)
        {
            throw std::runtime_error{fmt::format("Failed to get flow info: {}", statusToString(status))};
        }
        auto const rate = info.config.common.grainRate;
        if ((info.config.common.format != MXL_DATA_FORMAT_VIDEO) || (rate.numerator == 0) || (rate.denominator == 0))
        {
            throw std::runtime_error{"Verification requires a video flow with a valid grain rate."};
        }
        if ((info.runtime.headIndex == MXL_UNDEFINED_INDEX) || (count >= std::numeric_limits<std::uint64_t>::max() - info.runtime.headIndex))
        {
            throw std::runtime_error{"Flow has no readable head index."};
        }

        auto const firstIndex = info.runtime.headIndex + 1;
        auto byteErrors = std::uint64_t{0};
        auto orderErrors = std::uint64_t{0};
        auto grainErrors = std::uint64_t{0};
        auto frameSize = std::size_t{0};
        auto referenceCount = std::size_t{0};
        auto expectedFrames = std::vector<std::size_t>{};
        auto nextFrames = std::vector<std::size_t>{};
        auto firstTime = std::chrono::steady_clock::time_point{};
        auto lastTime = firstTime;
        auto minMs = std::numeric_limits<double>::max();
        auto maxMs = std::numeric_limits<double>::lowest();
        auto meanMs = 0.0;
        for (auto n = std::uint64_t{0}; n < count; ++n)
        {
            auto grain = ::mxlGrainInfo{};
            std::uint8_t* payload = nullptr;
            auto const index = firstIndex + n;
            auto const status = ::mxlFlowReaderGetGrain(reader.get(), index, timeoutNs, &grain, &payload);
            if (status != MXL_STATUS_OK)
            {
                throw std::runtime_error{fmt::format("Failed to read grain {} after {} frames: {}", index, n, statusToString(status))};
            }
            lastTime = std::chrono::steady_clock::now();
            auto const now = ::mxlGetTime();
            if (n == 0)
            {
                firstTime = lastTime;
            }
            auto const start = ::mxlIndexToTimestamp(&rate, index);
            if (start == MXL_UNDEFINED_INDEX)
            {
                throw std::runtime_error{"Failed to convert grain index to a timestamp."};
            }
            auto const latencyMs = (now >= start) ? static_cast<double>(now - start) / 1'000'000.0 : -static_cast<double>(start - now) / 1'000'000.0;
            minMs = std::min(minMs, latencyMs);
            maxMs = std::max(maxMs, latencyMs);
            meanMs += (latencyMs - meanMs) / static_cast<double>(n + 1);
            if ((grain.index != index) || (grain.flags & MXL_GRAIN_FLAG_INVALID) || (grain.validSlices != grain.totalSlices))
            {
                ++grainErrors;
                expectedFrames.clear();
                continue;
            }
            if (frameSize == 0)
            {
                frameSize = grain.grainSize;
                if ((frameSize == 0) || (reference.size() % frameSize != 0))
                {
                    throw std::runtime_error{"Reference file must contain complete frames matching the flow's grain size."};
                }
                referenceCount = reference.size() / frameSize;
                if ((referenceCount > 1) && (std::memcmp(reference.data(), reference.data() + frameSize, reference.size() - frameSize) == 0))
                {
                    referenceCount = 1;
                }
            }
            if ((grain.grainSize != frameSize) || (payload == nullptr))
            {
                ++byteErrors;
                expectedFrames.clear();
                continue;
            }
            nextFrames.clear();
            for (auto const expected : expectedFrames)
            {
                if (std::memcmp(payload, reference.data() + (expected * frameSize), frameSize) == 0)
                {
                    nextFrames.push_back((expected + 1) % referenceCount);
                }
            }
            if (nextFrames.empty())
            {
                // Keep every matching position until repeated frames can be distinguished.
                for (auto frame = std::size_t{0}; frame < referenceCount; ++frame)
                {
                    if (std::memcmp(payload, reference.data() + (frame * frameSize), frameSize) == 0)
                    {
                        nextFrames.push_back((frame + 1) % referenceCount);
                    }
                }
                if (nextFrames.empty())
                {
                    ++byteErrors;
                }
                else if (!expectedFrames.empty())
                {
                    ++orderErrors;
                }
            }
            expectedFrames.swap(nextFrames);
        }

        auto const span = std::chrono::duration<double>{lastTime - firstTime}.count();
        auto const expectedSpan = static_cast<double>(count - 1) * rate.denominator / rate.numerator;
        auto const tolerance = expectedSpan * 0.01;
        auto const rateOk = (span > 0) && (span >= expectedSpan - tolerance) && (span <= expectedSpan + tolerance);
        std::cout << fmt::format("frames_checked={} fps={:.2f} byte_errors={} order_errors={} grain_errors={} rate_ok={} "
                                 "latency_min_ms={:.3f} latency_mean_ms={:.3f} latency_max_ms={:.3f} jitter_ms={:.3f}\n",
            count,
            (span > 0) ? (count - 1) / span : 0.0,
            byteErrors,
            orderErrors,
            grainErrors,
            rateOk,
            minMs,
            meanMs,
            maxMs,
            maxMs - minMs);
        return ((byteErrors == 0) && (orderErrors == 0) && (grainErrors == 0) && rateOk) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

}

/**
 * @brief Parse probe options and verify a video flow.
 * @param argc Number of command-line arguments, including the executable name.
 * @param argv Command-line argument strings.
 * @return EXIT_SUCCESS if verification passes, a CLI parse status, or EXIT_FAILURE on error.
 */
int main(int argc, char** argv)
try
{
    auto app = CLI::App{"Verify video grains and report delivery rate, latency and jitter", "mxl-video-probe"};
    app.footer("MXL URI format:\n"
               "    mxl://[authority[:port]]/domain[?id=...]\n"
               "    See: https://github.com/dmf-mxl/mxl/docs/Addressability.md");

    auto version = ::mxlVersionType{};
    ::mxlGetVersion(&version);
    app.set_version_flag("--version", version.full);

    auto domain = std::string{};
    app.add_option("-d,--domain", domain, "The MXL domain directory")->check(CLI::ExistingDirectory);
    auto flowId = std::string{};
    app.add_option("-f,--flow", flowId, "The video flow id to read");
    auto count = std::uint64_t{60};
    app.add_option("-c,--count", count, "Number of video grains to verify, starting after the current head")
        ->check(CLI::Range(std::uint64_t{60}, std::numeric_limits<std::uint64_t>::max()));
    auto timeoutMs = std::uint64_t{1000};
    app.add_option("-t,--timeout-ms", timeoutMs, "Timeout per grain read in milliseconds")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max() / std::uint64_t{1'000'000}));
    auto referencePath = std::string{};
    app.add_option("--reference-file", referencePath, "Raw file containing a repeating sequence of complete frames")
        ->required()
        ->check(CLI::ExistingFile);
    auto address = std::vector<std::string>{};
    app.add_option("ADDRESS", address, "MXL URI")->expected(-1);

    CLI11_PARSE(app, argc, argv);

    // URI values take precedence over domain and flow options.
    if (!address.empty())
    {
        auto const parsedUri = ada::parse<ada::url_aggregator>(address.at(0));
        if (!parsedUri)
        {
            fmt::print(stderr, "ERROR: Invalid MXL URI.\n");
            return EXIT_FAILURE;
        }
        auto const path = parsedUri->get_pathname();
        if (path.empty())
        {
            fmt::print(stderr, "ERROR: Domain must be specified in the MXL URI.\n");
            return EXIT_FAILURE;
        }
        if (!parsedUri->get_hostname().empty() || !parsedUri->get_port().empty())
        {
            fmt::print(stderr, "ERROR: Authority/port not currently supported in MXL URI.\n");
            return EXIT_FAILURE;
        }
        domain = std::string{path};
        auto query = ada::url_search_params{parsedUri->get_search()};
        if (auto const id = query.get("id"))
        {
            flowId = std::string{*id};
        }
    }
    if (domain.empty())
    {
        fmt::print(stderr, "ERROR: Domain must be specified either via --domain or in the URI.\n");
        return EXIT_FAILURE;
    }
    if (flowId.empty())
    {
        fmt::print(stderr, "ERROR: Flow id must be specified either via --flow or in the URI id query parameter.\n");
        return EXIT_FAILURE;
    }
    return verifyVideoFlow(domain, flowId, referencePath, count, timeoutMs * 1'000'000ULL);
}
catch (std::exception const& ex)
{
    std::cerr << "ERROR: " << ex.what() << '\n';
    return EXIT_FAILURE;
}
catch (...)
{
    std::cerr << "ERROR: Unexpected exception.\n";
    return EXIT_FAILURE;
}
