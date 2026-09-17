// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Live producer fixtures for command-line probe integration checks.
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <exception>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>

namespace
{
    /** @brief Own an isolated temporary domain for the probe integration test. */
    struct Domain
    {
        std::filesystem::path path; ///< Directory removed after writers and child checks finish.

        /** @brief Create a unique domain directory; throw if creation fails. */
        Domain()
        {
            auto name = (std::filesystem::temp_directory_path() / "mxl-data-probe-XXXXXX").string();
            if (!::mkdtemp(name.data()))
            {
                throw std::runtime_error{"Cannot create test domain"};
            }
            path = name;
        }

        /// Remove the temporary domain without propagating cleanup failures.
        ~Domain()
        {
            auto error = std::error_code{};
            std::filesystem::remove_all(path, error);
        }
    };

    /**
     * @brief Convert a failed fixture API call into a test failure.
     * @param status MXL result to check.
     * @throws std::runtime_error status is not MXL_STATUS_OK.
     */
    void check(mxlStatus status)
    {
        if (status != MXL_STATUS_OK)
        {
            throw std::runtime_error{"MXL status " + std::to_string(status)};
        }
    }

    /** @brief Own a producer instance and keep one fixture flow alive during CLI checks. */
    struct Writer
    {
        mxlInstance instance{}; ///< Owned instance in the temporary test domain.
        mxlFlowWriter writer{}; ///< Owned producer handle, null until create succeeds.

        /**
         * @brief Create the producer instance.
         * @param domain Existing temporary domain directory.
         * @throws std::runtime_error Instance creation fails.
         */
        explicit Writer(char const* domain)
            : instance{mxlCreateInstance(domain, "{}")}
        {
            if (!instance)
            {
                throw std::runtime_error{"Cannot create test instance"};
            }
        }

        /// Release the producer handle and instance after child probes have exited.
        ~Writer()
        {
            if (writer)
            {
                mxlReleaseFlowWriter(instance, writer);
            }
            mxlDestroyInstance(instance);
        }

        /// Copying is disabled to preserve unique ownership of the API handles.
        Writer(Writer const&) = delete;
        /// Copy assignment is disabled to preserve unique ownership of the API handles.
        Writer& operator=(Writer const&) = delete;

        /**
         * @brief Create a fixture flow using a rate of 25 entries or grains per second.
         * @param id UUID to include in the fixture descriptor.
         * @param event True for an event flow; false for an ANC data flow.
         * @pre No writer has previously been created by this object.
         * @throws std::runtime_error Flow creation fails.
         */
        void create(char const* id, bool event)
        {
            auto const definition = std::string{"{\"id\":\""} + id +
                                    "\",\"label\":\"Probe test\",\"tags\":{\"urn:x-nmos:tag:grouphint/v1.0\":[\"Test:Probe\"]},"
                                    "\"grain_rate\":{\"numerator\":25},\"format\":\"urn:x-nmos:format:" +
                                    (event ? "event\"}" : "data\",\"media_type\":\"video/smpte291\"}");
            check(mxlCreateFlowWriter(instance, definition.c_str(), nullptr, &writer, nullptr, nullptr));
        }

        /**
         * @brief Publish one fixture event entry, including arbitrary fragment metadata.
         * @param timestamp Nondecreasing timestamp in TAI nanoseconds.
         * @param bytes Payload bytes for the entry.
         * @param offset Logical byte offset within the event.
         * @param complete True for an unfragmented event or the final fragment.
         * @param registry Registry identifying the DIT namespace.
         * @param type DIT bytes, including a possible full 256-byte nonterminated string.
         * @throws std::runtime_error Data exceeds capacity or an API call fails.
         */
        void event(std::uint64_t timestamp, std::span<std::uint8_t const> bytes, std::uint32_t offset, bool complete,
            mxlEventRegistryType registry = MXL_EVENT_REGISTRY_TYPE_SMPTE, std::string_view type = "010203")
        {
            auto info = mxlEventInfo{};
            auto payload = static_cast<std::uint8_t*>(nullptr);
            check(mxlFlowWriterOpenEvent(writer, &info, &payload));
            if (bytes.size() > info.eventSize || type.size() > sizeof info.dataItemType)
            {
                throw std::runtime_error{"Test event exceeds capacity"};
            }
            info.timestamp = timestamp;
            info.registryType = registry;
            std::memcpy(info.dataItemType, type.data(), type.size());
            info.eventSize = static_cast<std::uint32_t>(bytes.size());
            info.offset = offset;
            info.complete = complete ? 1 : 0;
            if (!bytes.empty())
            {
                std::memcpy(payload, bytes.data(), bytes.size());
            }
            check(mxlFlowWriterCommitEvent(writer, &info));
        }

        /**
         * @brief Commit a complete RFC-8331 grain containing no ANC elements.
         * @param index Grain index to publish in the fixture's data flow.
         * @throws std::runtime_error Opening or committing the grain fails.
         */
        void grain(std::uint64_t index)
        {
            auto info = mxlGrainInfo{};
            auto payload = static_cast<std::uint8_t*>(nullptr);
            check(mxlFlowWriterOpenGrain(writer, index, &info, &payload));
            // An RFC-8331 header containing zero ANC elements.
            std::memset(payload, 0, info.grainSize);
            info.validSlices = info.totalSlices;
            check(mxlFlowWriterCommitGrain(writer, &info));
        }
    };
}

/**
 * @brief Keep fixture producers alive while running the CMake probe checks in a child process.
 * @param argc Argument count, including the executable name.
 * @param argv CMake executable followed by its arguments; a domain definition is inserted automatically.
 * @return Child exit status, or EXIT_FAILURE if fixture setup or process management fails.
 */
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        return EXIT_FAILURE;
    }
    try
    {
        auto const domain = Domain{};
        auto events = Writer{domain.path.c_str()};
        events.create("cabbc00d-3860-4438-bc48-8ebdfe67305e", true);
        auto const bytes = std::array<std::uint8_t, 5>{0x00, 0x7F, 0x80, 0xFE, 0xFF};
        events.event(9000, bytes, 0, true);
        events.event(10000, bytes, 0, false);
        events.event(10000, std::span{bytes}.first(3), 5, true);
        events.event(10001, {}, 0, true, MXL_EVENT_REGISTRY_TYPE_MXL, "x-mxl:empty");
        auto longType = std::array<char, 256>{};
        longType.fill('X');
        events.event(10002, std::span{bytes}.first(1), 0, true, MXL_EVENT_REGISTRY_TYPE_MXL, std::string_view{longType.data(), longType.size()});

        auto empty = Writer{domain.path.c_str()};
        empty.create("cabbc00d-3860-4438-bc48-8ebdfe67305f", true);

        auto anc = Writer{domain.path.c_str()};
        anc.create("db3bd465-2772-484f-8fac-830b0471258b", false);
        anc.grain(41);
        anc.grain(42);
        // Flow release removes the files, so keep the producers open throughout the CLI checks.
        auto arguments = std::vector<std::string>{argv[1], "-Ddomain=" + domain.path.string()};
        for (auto i = 2; i < argc; ++i)
        {
            arguments.emplace_back(argv[i]);
        }
        auto pointers = std::vector<char*>{};
        for (auto& argument : arguments)
        {
            pointers.push_back(argument.data());
        }
        pointers.push_back(nullptr);
        auto environment = std::array<char*, 1>{nullptr};
        auto child = pid_t{};
        if (::posix_spawn(&child, pointers[0], nullptr, nullptr, pointers.data(), environment.data()) != 0)
        {
            throw std::runtime_error{"Cannot start probe test script"};
        }
        auto status = int{};
        while (::waitpid(child, &status, 0) < 0)
        {
            if (errno != EINTR)
            {
                throw std::runtime_error{"Cannot wait for probe test script"};
            }
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
    }
    catch (std::exception const& ex)
    {
        (void)std::fprintf(stderr, "%s\n", ex.what());
        return EXIT_FAILURE;
    }
}
