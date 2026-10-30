// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Windows section lifetime checks through writer release and failed acquisition.
 */

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/Instance.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/PosixFlowIoFactory.hpp"
#include "mxl-internal/SharedMemory.hpp"
#include "Utils.hpp"

namespace
{
    using namespace mxl::lib;

    /// Owns a public instance, including any readers or writers left open by a failed assertion.
    using OwnedInstance = std::unique_ptr<std::remove_pointer_t<mxlInstance>, decltype(&mxlDestroyInstance)>;

    /**
     * Create an instance whose handles are released at scope exit.
     * @param domain Existing directory of the test domain.
     * @return An owned instance with a short history to keep video allocations small.
     */
    OwnedInstance createInstance(std::filesystem::path const& domain)
    {
        auto instance = OwnedInstance{
            mxlCreateInstance(domain.string().c_str(), R"({"urn:x-mxl:option:history_duration/v1.0":100000000})"), &mxlDestroyInstance};
        REQUIRE(instance);
        return instance;
    }

    /**
     * Find the anchors that name a flow's in-memory sections.
     * @param flowDirectory Directory of the published flow.
     * @return All section anchors, excluding its descriptor and access notification file.
     */
    std::vector<std::filesystem::path> sectionAnchors(std::filesystem::path const& flowDirectory)
    {
        auto anchors = std::vector<std::filesystem::path>{};
        for (auto const& entry : std::filesystem::recursive_directory_iterator{flowDirectory})
        {
            if (entry.is_regular_file() && (entry.path().filename() != FLOW_DESCRIPTOR_FILE_NAME) &&
                (entry.path().filename() != FLOW_ACCESS_FILE_NAME))
            {
                anchors.push_back(entry.path());
            }
        }
        REQUIRE(anchors.size() >= 2);
        return anchors;
    }

    /**
     * Keep each anchor's file identity accessible after its flow directory is deleted, without holding its section open.
     * @param domain Existing test domain in which to place the hard links.
     * @param flowDirectory Directory of the published flow.
     * @return Hard links used to probe every section of the deleted flow.
     */
    std::vector<std::filesystem::path> linkAnchors(std::filesystem::path const& domain, std::filesystem::path const& flowDirectory)
    {
        static auto nextLink = std::size_t{0};
        auto links = std::vector<std::filesystem::path>{};
        for (auto const& anchor : sectionAnchors(flowDirectory))
        {
            auto const link = domain / ("section-" + std::to_string(nextLink++));
            std::filesystem::create_hard_link(anchor, link);
            links.push_back(link);
        }
        return links;
    }

    /**
     * Check section lifetime without retaining a probe mapping between assertions.
     * @param anchors Paths with the identities of the original section anchors.
     * @param alive Whether each section must still be openable.
     */
    void checkSections(std::vector<std::filesystem::path> const& anchors, bool alive)
    {
        for (auto const& anchor : anchors)
        {
            INFO(anchor.string());
            if (alive)
            {
                REQUIRE_NOTHROW(SharedMemorySegment{anchor.string().c_str(), AccessMode::READ_ONLY, 0, LockMode::None});
            }
            else
            {
                REQUIRE_THROWS_AS((SharedMemorySegment{anchor.string().c_str(), AccessMode::READ_ONLY, 0, LockMode::None}), SectionMissingError);
            }
        }
    }

    /** @brief Inject a writer construction failure after the flow sections are created. */
    class RejectingWriterFactory final : public PosixFlowIoFactory
    {
    public:
        /**
         * Create a factory with the instance's normal domain watcher.
         * @param watcher Watcher shared with the test instance.
         */
        explicit RejectingWriterFactory(DomainWatcher::ptr const& watcher)
            : PosixFlowIoFactory{watcher}
        {}

        /**
         * Reject an audio writer after flow creation succeeds.
         * @param manager Manager that created the flow.
         * @param flowId Identifier of the created flow.
         * @param data Flow mappings owned by the acquisition attempt.
         * @return No writer is returned.
         * @throws std::runtime_error Always, to exercise acquisition cleanup.
         */
        std::unique_ptr<ContinuousFlowWriter> createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const override
        {
            static_cast<void>(manager);
            static_cast<void>(flowId);
            static_cast<void>(data);
            throw std::runtime_error{"Injected writer construction failure."};
        }
    };
}

/** @test Releasing either instance's local writer frees all sections after the remaining writer and optional reader close. */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Windows section lifetime: released writers do not pin deleted flows",
    "[windows][lifetime]")
{
    auto const definitionFile = GENERATE("data/v210_flow.json", "data/audio_flow.json");
    auto const creatorReleasesFirst = GENERATE(false, true);
    auto const keepReader = GENERATE(false, true);
    CAPTURE(definitionFile, creatorReleasesFirst, keepReader);

    auto const definition = mxl::tests::readFile(definitionFile);
    auto const creator = createInstance(domain);
    auto const opener = createInstance(domain);
    auto creatorWriter = mxlFlowWriter{};
    auto creatorReference = mxlFlowWriter{};
    auto openerWriter = mxlFlowWriter{};
    auto config = mxlFlowConfigInfo{};
    auto created = bool{};
    REQUIRE(mxlCreateFlowWriter(creator.get(), definition.c_str(), nullptr, &creatorWriter, &config, &created) == MXL_STATUS_OK);
    REQUIRE(created);
    auto const id = uuids::to_string(config.common.id);
    auto const flowDirectory = makeFlowDirectoryName(domain, id);
    auto const links = linkAnchors(domain, flowDirectory);

    REQUIRE(mxlCreateFlowWriter(creator.get(), definition.c_str(), nullptr, &creatorReference, nullptr, &created) == MXL_STATUS_OK);
    REQUIRE_FALSE(created);
    REQUIRE(creatorReference == creatorWriter);
    REQUIRE(mxlReleaseFlowWriter(creator.get(), creatorReference) == MXL_STATUS_OK);
    checkSections(links, true);

    REQUIRE(mxlCreateFlowWriter(opener.get(), definition.c_str(), nullptr, &openerWriter, nullptr, &created) == MXL_STATUS_OK);
    REQUIRE_FALSE(created);
    auto reader = mxlFlowReader{};
    if (keepReader)
    {
        REQUIRE(mxlCreateFlowReader(creator.get(), id.c_str(), nullptr, &reader) == MXL_STATUS_OK);
    }

    auto const firstInstance = creatorReleasesFirst ? creator.get() : opener.get();
    auto const firstWriter = creatorReleasesFirst ? creatorWriter : openerWriter;
    auto const lastInstance = creatorReleasesFirst ? opener.get() : creator.get();
    auto const lastWriter = creatorReleasesFirst ? openerWriter : creatorWriter;
    REQUIRE(mxlReleaseFlowWriter(firstInstance, firstWriter) == MXL_STATUS_OK);
    REQUIRE(std::filesystem::exists(flowDirectory));
    checkSections(links, true);

    REQUIRE(mxlReleaseFlowWriter(lastInstance, lastWriter) == MXL_STATUS_OK);
    REQUIRE_FALSE(std::filesystem::exists(flowDirectory));
    checkSections(links, keepReader);
    if (keepReader)
    {
        auto runtime = mxlFlowRuntimeInfo{};
        REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtime) == MXL_STATUS_OK);
        REQUIRE(mxlReleaseFlowReader(creator.get(), reader) == MXL_STATUS_OK);
        checkSections(links, false);
    }

    // The instances stay alive. Reusing the ID must not keep sections of the deleted generation.
    REQUIRE(mxlCreateFlowWriter(creator.get(), definition.c_str(), nullptr, &creatorWriter, nullptr, &created) == MXL_STATUS_OK);
    REQUIRE(created);
    checkSections(links, false);
    REQUIRE(mxlReleaseFlowWriter(creator.get(), creatorWriter) == MXL_STATUS_OK);
}

/** @test A failed writer constructor releases the flow sections while its instance remains alive. */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Windows section lifetime: failed writer acquisition releases sections",
    "[windows][lifetime]")
{
    auto const definition = mxl::tests::readFile("data/audio_flow.json");
    auto const watcher = std::make_shared<DomainWatcher>(domain);
    auto factory = std::make_unique<RejectingWriterFactory>(watcher);
    auto instance = Instance{domain, "", std::move(factory), watcher};
    REQUIRE_THROWS_AS(instance.createFlowWriter(definition, std::nullopt), std::runtime_error);
    auto const flowDirectory = makeFlowDirectoryName(domain, "b3bb5be7-9fe9-4324-a5bb-4c70e1084449");
    checkSections(sectionAnchors(flowDirectory), false);
}
