// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "Utils.hpp"

namespace
{
    /**
     * A test case for one discrete flow definition, with the plane count its storage layout must report.
     */
    struct DiscreteFlowCase
    {
        char const* flowDefFile;  ///< Path of the flow definition, relative to the test working directory.
        char const* flowId;       ///< The id found in the flow definition.
        std::uint32_t planeCount; ///< The expected number of planes.
    };

    /**
     * Holds a writer instance, a separate reader instance, and a writer and reader for one flow.
     * Releases everything on destruction.
     */
    class FlowPair
    {
    public:
        /**
         * Create the instances, the writer and the reader.
         * \param[in] domain The MXL domain.
         * \param[in] flowDefFile Path of the flow definition to create.
         * \param[in] flowId The id found in the flow definition.
         * \param[in] writerOptions Options passed to mxlCreateFlowWriter().
         * \param[in] readerOptions Options passed to mxlCreateFlowReader().
         */
        FlowPair(std::filesystem::path const& domain, char const* flowDefFile, char const* flowId, char const* writerOptions = "",
            char const* readerOptions = "")
            : _writerInstance{mxlCreateInstance(domain.string().c_str(), "")}
            , _readerInstance{mxlCreateInstance(domain.string().c_str(), "")}
            , _writer{nullptr}
            , _reader{nullptr}
            , _configInfo{}
        {
            REQUIRE(_writerInstance != nullptr);
            REQUIRE(_readerInstance != nullptr);

            auto const flowDef = mxl::tests::readFile(flowDefFile);
            REQUIRE(mxlCreateFlowWriter(_writerInstance, flowDef.c_str(), writerOptions, &_writer, &_configInfo, nullptr) == MXL_STATUS_OK);
            REQUIRE(mxlCreateFlowReader(_readerInstance, flowId, readerOptions, &_reader) == MXL_STATUS_OK);
        }

        /** Release the reader, the writer and both instances. */
        ~FlowPair()
        {
            mxlReleaseFlowReader(_readerInstance, _reader);
            mxlReleaseFlowWriter(_writerInstance, _writer);
            mxlDestroyInstance(_readerInstance);
            mxlDestroyInstance(_writerInstance);
        }

        FlowPair(FlowPair const&) = delete;
        FlowPair(FlowPair&&) = delete;
        FlowPair& operator=(FlowPair const&) = delete;
        FlowPair& operator=(FlowPair&&) = delete;

        /** \return The flow writer. */
        [[nodiscard]]
        mxlFlowWriter writer() const noexcept
        {
            return _writer;
        }

        /** \return The flow reader. */
        [[nodiscard]]
        mxlFlowReader reader() const noexcept
        {
            return _reader;
        }

        /** \return The configuration returned when the writer was created. */
        [[nodiscard]]
        mxlFlowConfigInfo const& configInfo() const noexcept
        {
            return _configInfo;
        }

    private:
        mxlInstance _writerInstance;   ///< Instance owning the writer.
        mxlInstance _readerInstance;   ///< Instance owning the reader.
        mxlFlowWriter _writer;         ///< The flow writer.
        mxlFlowReader _reader;         ///< The flow reader.
        mxlFlowConfigInfo _configInfo; ///< Configuration of the created flow.
    };

    /**
     * \return The discrete flow cases covered by the tests: progressive v210, v210 with key, and data.
     */
    auto discreteFlowCases()
    {
        return Catch::Generators::values<DiscreteFlowCase>({
            {.flowDefFile = "data/v210_flow.json",  .flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed", .planeCount = 1U},
            {.flowDefFile = "data/v210a_flow.json", .flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed", .planeCount = 2U},
            {.flowDefFile = "data/data_flow.json",  .flowId = "db3bd465-2772-484f-8fac-830b0471258b", .planeCount = 1U},
        });
    }

    /**
     * Check that two host slot descriptions are equal, field by field.
     * \param[in] lhs The first description.
     * \param[in] rhs The second description.
     */
    void requireSameHostSlot(mxlGrainStorage const& lhs, mxlGrainStorage const& rhs)
    {
        REQUIRE(lhs.version == rhs.version);
        REQUIRE(lhs.size == rhs.size);
        REQUIRE(lhs.storageType == rhs.storageType);
        REQUIRE(lhs.slot == rhs.slot);
        REQUIRE(lhs.host.pointer == rhs.host.pointer);
    }
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Layout describes host storage", "[mxl flows][grain storage]")
{
    auto const flowCase = GENERATE(discreteFlowCases());
    CAPTURE(flowCase.flowDefFile);
    auto const flows = FlowPair{domain, flowCase.flowDefFile, flowCase.flowId};

    auto writerLayout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowWriterGetStorageLayout(flows.writer(), &writerLayout) == MXL_STATUS_OK);
    auto readerLayout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowReaderGetStorageLayout(flows.reader(), &readerLayout) == MXL_STATUS_OK);

    // Both sides see the same layout.
    REQUIRE(std::memcmp(&writerLayout, &readerLayout, sizeof(mxlGrainStorageLayout)) == 0);

    auto const& discrete = flows.configInfo().discrete;
    REQUIRE(writerLayout.version == 1U);
    REQUIRE(writerLayout.size == sizeof(mxlGrainStorageLayout));
    REQUIRE(writerLayout.storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER);
    REQUIRE(writerLayout.deviceIndex == -1);
    REQUIRE(writerLayout.slotCount == discrete.grainCount);
    REQUIRE(writerLayout.planeCount == flowCase.planeCount);

    // Host storage is not tied to a device.
    for (auto const byte : writerLayout.deviceUuid)
    {
        REQUIRE(byte == 0U);
    }

    // Planes follow each other and together cover the whole grain payload.
    auto grainInfo = mxlGrainInfo{};
    REQUIRE(mxlFlowWriterGetGrainInfo(flows.writer(), 0U, &grainInfo) == MXL_STATUS_OK);

    auto expectedOffset = std::uint64_t{0};
    for (auto plane = std::uint32_t{0}; plane < writerLayout.planeCount; ++plane)
    {
        CAPTURE(plane);
        REQUIRE(writerLayout.planes[plane].offset == expectedOffset);
        REQUIRE(writerLayout.planes[plane].pitch == discrete.sliceSizes[plane]);
        REQUIRE(writerLayout.planes[plane].reserved == 0U);
        REQUIRE(writerLayout.planes[plane].size == (std::uint64_t{discrete.sliceSizes[plane]} * grainInfo.totalSlices));
        expectedOffset += writerLayout.planes[plane].size;
    }
    REQUIRE(expectedOffset == grainInfo.grainSize);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Mapped slots match the pointer based API", "[mxl flows][grain storage]")
{
    auto const flowCase = GENERATE(discreteFlowCases());
    CAPTURE(flowCase.flowDefFile);
    auto const flows = FlowPair{domain, flowCase.flowDefFile, flowCase.flowId};

    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowWriterGetStorageLayout(flows.writer(), &layout) == MXL_STATUS_OK);

    // The writer maps all slots up front. Each entry describes the slot with the same index.
    // Parentheses select the constructor that creates slotCount entries.
    auto writerSlots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowWriterMapSlots(flows.writer(), layout.slotCount, writerSlots.data()) == MXL_STATUS_OK);
    for (auto i = std::uint32_t{0}; i < layout.slotCount; ++i)
    {
        CAPTURE(i);
        REQUIRE(writerSlots[i].version == 1U);
        REQUIRE(writerSlots[i].size == sizeof(mxlGrainStorage));
        REQUIRE(writerSlots[i].storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER);
        REQUIRE(writerSlots[i].slot == i);
        REQUIRE(writerSlots[i].host.pointer != nullptr);
    }

    auto const rate = flows.configInfo().common.grainRate;
    auto const index = mxlGetCurrentIndex(&rate);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    // The pointer based API returns the address of the slot that mxlFlowWriterOpenGrainSlot() reports.
    auto grainInfo = mxlGrainInfo{};
    std::uint8_t* writerPayload = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(flows.writer(), index, &grainInfo, &writerPayload) == MXL_STATUS_OK);

    auto writerSlot = std::uint32_t{0};
    REQUIRE(mxlFlowWriterOpenGrainSlot(flows.writer(), index, &grainInfo, &writerSlot) == MXL_STATUS_OK);
    REQUIRE(writerSlot == (index % layout.slotCount));
    REQUIRE(writerSlots[writerSlot].host.pointer == writerPayload);

    // Mark the first and last byte of each plane through the mapped slot, then commit.
    auto* const writerBase = static_cast<std::uint8_t*>(writerSlots[writerSlot].host.pointer);
    for (auto plane = std::uint32_t{0}; plane < layout.planeCount; ++plane)
    {
        writerBase[layout.planes[plane].offset] = static_cast<std::uint8_t>(0xA0U + plane);
        writerBase[layout.planes[plane].offset + layout.planes[plane].size - 1U] = static_cast<std::uint8_t>(0xB0U + plane);
    }
    grainInfo.validSlices = grainInfo.totalSlices;
    REQUIRE(mxlFlowWriterCommitGrain(flows.writer(), &grainInfo) == MXL_STATUS_OK);

    // The reader maps all slots before reading. Mapping again returns the same descriptions.
    auto readerSlots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowReaderMapSlots(flows.reader(), layout.slotCount, readerSlots.data()) == MXL_STATUS_OK);
    auto readerSlotsAgain = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowReaderMapSlots(flows.reader(), layout.slotCount, readerSlotsAgain.data()) == MXL_STATUS_OK);
    for (auto i = std::uint32_t{0}; i < layout.slotCount; ++i)
    {
        CAPTURE(i);
        requireSameHostSlot(readerSlots[i], readerSlotsAgain[i]);
    }

    // The reader finds the grain in the slot that the writer used, at the address the pointer based API returns.
    auto readInfo = mxlGrainInfo{};
    std::uint8_t* readerPayload = nullptr;
    REQUIRE(mxlFlowReaderGetGrain(flows.reader(), index, 0U, &readInfo, &readerPayload) == MXL_STATUS_OK);

    auto readerSlot = std::uint32_t{0};
    REQUIRE(mxlFlowReaderGetGrainSlot(flows.reader(), index, MXL_GRAIN_VALID_SLICES_ALL, 0U, &readInfo, &readerSlot) == MXL_STATUS_OK);
    REQUIRE(readInfo.index == index);
    REQUIRE(readerSlot == writerSlot);
    REQUIRE(readerSlots[readerSlot].host.pointer == readerPayload);

    auto const* const readerBase = static_cast<std::uint8_t const*>(readerSlots[readerSlot].host.pointer);
    for (auto plane = std::uint32_t{0}; plane < layout.planeCount; ++plane)
    {
        CAPTURE(plane);
        REQUIRE(readerBase[layout.planes[plane].offset] == static_cast<std::uint8_t>(0xA0U + plane));
        REQUIRE(readerBase[layout.planes[plane].offset + layout.planes[plane].size - 1U] == static_cast<std::uint8_t>(0xB0U + plane));
    }
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Invalid arguments", "[mxl flows][grain storage]")
{
    auto const flows = FlowPair{domain, "data/v210_flow.json", "5fbec3b1-1b0f-417d-9059-8b94a47197ed"};

    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowReaderGetStorageLayout(flows.reader(), &layout) == MXL_STATUS_OK);

    // One entry more than the flow has slots, so that both too small and too large counts can be passed.
    auto slots = std::vector<mxlGrainStorage>(layout.slotCount + 1U);
    auto grainInfo = mxlGrainInfo{};
    auto slot = std::uint32_t{0};

    // The slot count must match the layout.
    REQUIRE(mxlFlowReaderMapSlots(flows.reader(), layout.slotCount - 1U, slots.data()) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowReaderMapSlots(flows.reader(), layout.slotCount + 1U, slots.data()) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterMapSlots(flows.writer(), layout.slotCount - 1U, slots.data()) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterMapSlots(flows.writer(), layout.slotCount + 1U, slots.data()) == MXL_ERR_INVALID_ARG);

    // Null output arguments.
    REQUIRE(mxlFlowReaderGetStorageLayout(flows.reader(), nullptr) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterGetStorageLayout(flows.writer(), nullptr) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowReaderMapSlots(flows.reader(), layout.slotCount, nullptr) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterMapSlots(flows.writer(), layout.slotCount, nullptr) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowReaderGetGrainSlot(flows.reader(), 0U, MXL_GRAIN_VALID_SLICES_ALL, 0U, nullptr, &slot) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowReaderGetGrainSlot(flows.reader(), 0U, MXL_GRAIN_VALID_SLICES_ALL, 0U, &grainInfo, nullptr) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterOpenGrainSlot(flows.writer(), 0U, nullptr, &slot) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterOpenGrainSlot(flows.writer(), 0U, &grainInfo, nullptr) == MXL_ERR_INVALID_ARG);

    // Null handles.
    REQUIRE(mxlFlowReaderGetStorageLayout(nullptr, &layout) == MXL_ERR_INVALID_FLOW_READER);
    REQUIRE(mxlFlowWriterGetStorageLayout(nullptr, &layout) == MXL_ERR_INVALID_FLOW_WRITER);
    REQUIRE(mxlFlowReaderMapSlots(nullptr, layout.slotCount, slots.data()) == MXL_ERR_INVALID_FLOW_READER);
    REQUIRE(mxlFlowWriterMapSlots(nullptr, layout.slotCount, slots.data()) == MXL_ERR_INVALID_FLOW_WRITER);
    REQUIRE(mxlFlowReaderGetGrainSlot(nullptr, 0U, MXL_GRAIN_VALID_SLICES_ALL, 0U, &grainInfo, &slot) == MXL_ERR_INVALID_FLOW_READER);
    REQUIRE(mxlFlowWriterOpenGrainSlot(nullptr, 0U, &grainInfo, &slot) == MXL_ERR_INVALID_FLOW_WRITER);

    // A zero timeout returns without waiting for a grain that is not written yet.
    auto const rate = flows.configInfo().common.grainRate;
    auto const futureIndex = mxlGetCurrentIndex(&rate) + 10U;
    REQUIRE(
        mxlFlowReaderGetGrainSlot(flows.reader(), futureIndex, MXL_GRAIN_VALID_SLICES_ALL, 0U, &grainInfo, &slot) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Not available for continuous flows", "[mxl flows][grain storage]")
{
    auto const flows = FlowPair{domain, "data/audio_flow.json", "b3bb5be7-9fe9-4324-a5bb-4c70e1084449"};

    auto layout = mxlGrainStorageLayout{};
    auto slots = std::vector<mxlGrainStorage>(1U);
    auto grainInfo = mxlGrainInfo{};
    auto slot = std::uint32_t{0};

    REQUIRE(mxlFlowReaderGetStorageLayout(flows.reader(), &layout) == MXL_ERR_INVALID_FLOW_READER);
    REQUIRE(mxlFlowWriterGetStorageLayout(flows.writer(), &layout) == MXL_ERR_INVALID_FLOW_WRITER);
    REQUIRE(mxlFlowReaderMapSlots(flows.reader(), 1U, slots.data()) == MXL_ERR_INVALID_FLOW_READER);
    REQUIRE(mxlFlowWriterMapSlots(flows.writer(), 1U, slots.data()) == MXL_ERR_INVALID_FLOW_WRITER);
    REQUIRE(mxlFlowReaderGetGrainSlot(flows.reader(), 0U, MXL_GRAIN_VALID_SLICES_ALL, 0U, &grainInfo, &slot) == MXL_ERR_INVALID_FLOW_READER);
    REQUIRE(mxlFlowWriterOpenGrainSlot(flows.writer(), 0U, &grainInfo, &slot) == MXL_ERR_INVALID_FLOW_WRITER);
}

namespace
{
    /** Id of the flow in data/v210_flow.json. */
    constexpr auto V210_FLOW_ID = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    /** Writer options that select the file backed test backend built by the tests. */
    constexpr auto TEST_BACKEND_OPTIONS = R"({"payload": {"backend": "test-file"}})";

    /**
     * Try to create a writer and return the status. Releases the writer if the call succeeds.
     * \param[in] domain The MXL domain.
     * \param[in] flowDefFile Path of the flow definition to create.
     * \param[in] options Options passed to mxlCreateFlowWriter().
     * \return The status returned by mxlCreateFlowWriter().
     */
    mxlStatus tryCreateWriter(std::filesystem::path const& domain, char const* flowDefFile, char const* options)
    {
        auto instance = mxlCreateInstance(domain.string().c_str(), "");
        REQUIRE(instance != nullptr);
        auto const flowDef = mxl::tests::readFile(flowDefFile);
        mxlFlowWriter writer = nullptr;
        auto const status = mxlCreateFlowWriter(instance, flowDef.c_str(), options, &writer, nullptr, nullptr);
        if (status == MXL_STATUS_OK)
        {
            mxlReleaseFlowWriter(instance, writer);
        }
        mxlDestroyInstance(instance);
        return status;
    }
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : A flow can use a payload backend", "[mxl flows][grain storage]")
{
    auto const flows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID, TEST_BACKEND_OPTIONS};

    // The flow uses flow data version 2, so that SDK releases without backends refuse it.
    auto info = mxlFlowInfo{};
    REQUIRE(mxlFlowReaderGetInfo(flows.reader(), &info) == MXL_STATUS_OK);
    REQUIRE(info.version == 2U);
    REQUIRE(info.config.common.payloadLocation == 0U);
    REQUIRE(info.config.common.deviceIndex == -1);

    // The flow directory holds the payload descriptor and the backend's own file, and grain files hold only their header.
    auto const flowDir = mxl::lib::makeFlowDirectoryName(domain, V210_FLOW_ID);
    REQUIRE(std::filesystem::exists(flowDir / "payload.json"));
    REQUIRE(std::filesystem::exists(flowDir / "payload.test"));
    REQUIRE(std::filesystem::file_size(mxl::lib::makeGrainDataFilePath(domain, V210_FLOW_ID, 0U)) == 8192U);

    // The layout comes from the backend, which reports a marker UUID.
    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowReaderGetStorageLayout(flows.reader(), &layout) == MXL_STATUS_OK);
    REQUIRE(layout.storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER);
    REQUIRE(layout.deviceUuid[0] == 0x10U);
    REQUIRE(layout.deviceUuid[15] == 0x1FU);

    auto writerSlots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowWriterMapSlots(flows.writer(), layout.slotCount, writerSlots.data()) == MXL_STATUS_OK);
    auto readerSlots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowReaderMapSlots(flows.reader(), layout.slotCount, readerSlots.data()) == MXL_STATUS_OK);

    // A grain written through the writer's mapping is read through the reader's.
    auto const rate = flows.configInfo().common.grainRate;
    auto const index = mxlGetCurrentIndex(&rate);
    auto grainInfo = mxlGrainInfo{};
    auto writerSlot = std::uint32_t{0};
    REQUIRE(mxlFlowWriterOpenGrainSlot(flows.writer(), index, &grainInfo, &writerSlot) == MXL_STATUS_OK);
    auto* const writerBytes = static_cast<std::uint8_t*>(writerSlots[writerSlot].host.pointer);
    writerBytes[0] = 0x42;
    writerBytes[grainInfo.grainSize - 1U] = 0x24;
    grainInfo.validSlices = grainInfo.totalSlices;
    REQUIRE(mxlFlowWriterCommitGrain(flows.writer(), &grainInfo) == MXL_STATUS_OK);

    auto readerSlot = std::uint32_t{0};
    REQUIRE(mxlFlowReaderGetGrainSlot(flows.reader(), index, MXL_GRAIN_VALID_SLICES_ALL, 0U, &grainInfo, &readerSlot) == MXL_STATUS_OK);
    REQUIRE(readerSlot == writerSlot);
    auto const* const readerBytes = static_cast<std::uint8_t const*>(readerSlots[readerSlot].host.pointer);
    REQUIRE(readerBytes != writerBytes);
    REQUIRE(readerBytes[0] == 0x42);
    REQUIRE(readerBytes[grainInfo.grainSize - 1U] == 0x24);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : A data flow can use a payload backend", "[mxl flows][grain storage]")
{
    // libmxl does not restrict backends to video. The test backend stores data flows too.
    auto const flows = FlowPair{domain, "data/data_flow.json", "db3bd465-2772-484f-8fac-830b0471258b", TEST_BACKEND_OPTIONS};

    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowReaderGetStorageLayout(flows.reader(), &layout) == MXL_STATUS_OK);
    REQUIRE(layout.storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER);
    REQUIRE(layout.planeCount == 1U);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Invalid payload options are rejected", "[mxl flows][grain storage]")
{
    // Malformed generic keys.
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": "device"})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"backend": "../escape"}})") == MXL_ERR_INVALID_ARG);

    // The built-in host storage accepts no other keys. There is no "location" key, and no default backend for device
    // memory: a writer that wants storage other than host memory names its backend.
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"location": "device"}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"location": "device", "backend": "host"}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"backend": "", "deviceUuid": "GPU-0"}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"deviceIndex": 0}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"unknown": 1}})") == MXL_ERR_INVALID_ARG);

    // Other keys belong to the backend, which validates them.
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"backend": "test-file", "alignment": 100}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"backend": "test-file", "deviceIndex": 0}})") == MXL_ERR_INVALID_ARG);

    // Backends that are not installed. This includes the CUDA backend, which this build does not provide.
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"backend": "does-not-exist"}})") == MXL_ERR_UNSUPPORTED_OPERATION);
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"backend": "cuda-linear"}})") == MXL_ERR_UNSUPPORTED_OPERATION);

    // Continuous flows only use the built-in host storage.
    REQUIRE(tryCreateWriter(domain, "data/audio_flow.json", TEST_BACKEND_OPTIONS) == MXL_ERR_UNSUPPORTED_OPERATION);

    // None of the failed attempts left a flow behind.
    REQUIRE_FALSE(flowDirectoryExists(V210_FLOW_ID));

    // The built-in host storage can be named explicitly.
    REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", R"({"payload": {"backend": "host"}})") == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Backend options reach the backend", "[mxl flows][grain storage]")
{
    auto const flows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID, R"({"payload": {"backend": "test-file", "alignment": 16384}})"};

    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowWriterGetStorageLayout(flows.writer(), &layout) == MXL_STATUS_OK);
    auto slots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowWriterMapSlots(flows.writer(), layout.slotCount, slots.data()) == MXL_STATUS_OK);

    // The test backend rounds the distance between slots up to the requested alignment.
    auto const distance = static_cast<std::uint8_t*>(slots[1].host.pointer) - static_cast<std::uint8_t*>(slots[0].host.pointer);
    REQUIRE((distance % 16384) == 0);
    REQUIRE(static_cast<std::uint64_t>(distance) >= layout.planes[0].size);

    // The backend reports no device, which libmxl records in the flow configuration.
    REQUIRE(flows.configInfo().common.deviceIndex == -1);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Invalid reader options are rejected", "[mxl flows][grain storage]")
{
    // Flows with the built-in host storage ignore the backend keys of reader options.
    auto const hostFlows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID, "", R"({"payload": {"deviceIndex": 0}})"};

    auto instance = mxlCreateInstance(domain.string().c_str(), "");
    REQUIRE(instance != nullptr);
    mxlFlowReader reader = nullptr;

    // Readers cannot select the storage.
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, R"({"payload": "any"})", &reader) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, R"({"payload": {"backend": "test-file"}})", &reader) == MXL_ERR_INVALID_ARG);
    mxlDestroyInstance(instance);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : The backend validates reader options", "[mxl flows][grain storage]")
{
    auto const flows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID, TEST_BACKEND_OPTIONS};

    // The test backend accepts no reader options.
    auto instance = mxlCreateInstance(domain.string().c_str(), "");
    REQUIRE(instance != nullptr);
    mxlFlowReader reader = nullptr;
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, R"({"payload": {"deviceIndex": 0}})", &reader) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, R"({"payload": {}})", &reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    mxlDestroyInstance(instance);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : A backend flow has one writer", "[mxl flows][grain storage]")
{
    {
        auto const flows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID, TEST_BACKEND_OPTIONS};

        // A second writer, with or without a backend, is refused while the first one exists.
        REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", TEST_BACKEND_OPTIONS) == MXL_ERR_CONFLICT);
        REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", "") == MXL_ERR_CONFLICT);
    }

    {
        // A writer that asks for a backend cannot join a flow with built-in storage.
        auto const flows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID};
        REQUIRE(tryCreateWriter(domain, "data/v210_flow.json", TEST_BACKEND_OPTIONS) == MXL_ERR_CONFLICT);
    }
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : A reader without the backend can read metadata",
    "[mxl flows][grain storage]")
{
    auto const flows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID, TEST_BACKEND_OPTIONS};

    // Make the flow name a backend that is not installed, as on a machine without the right device runtime.
    auto const descriptorPath = mxl::lib::makeFlowDirectoryName(domain, V210_FLOW_ID) / "payload.json";
    auto descriptor = mxl::tests::readFile(descriptorPath);
    auto const pos = descriptor.find("test-file");
    REQUIRE(pos != std::string::npos);
    descriptor.replace(pos, std::strlen("test-file"), "not-installed");
    {
        auto out = std::ofstream{descriptorPath, std::ios::out | std::ios::trunc};
        out << descriptor;
    }

    auto instance = mxlCreateInstance(domain.string().c_str(), "");
    REQUIRE(instance != nullptr);
    mxlFlowReader reader = nullptr;
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, "", &reader) == MXL_STATUS_OK);

    // Metadata is available.
    auto info = mxlFlowInfo{};
    REQUIRE(mxlFlowReaderGetInfo(reader, &info) == MXL_STATUS_OK);
    REQUIRE(info.version == 2U);

    // The payload is not, and the error says why.
    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowReaderGetStorageLayout(reader, &layout) == MXL_ERR_UNSUPPORTED_OPERATION);
    auto slots = std::vector<mxlGrainStorage>(info.config.discrete.grainCount);
    REQUIRE(mxlFlowReaderMapSlots(reader, info.config.discrete.grainCount, slots.data()) == MXL_ERR_UNSUPPORTED_OPERATION);

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    mxlDestroyInstance(instance);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Grain storage : Descriptors in an unknown format are refused",
    "[mxl flows][grain storage]")
{
    // Writer and reader can come from different SDK releases. Each descriptor records the version of its format.
    auto const flows = FlowPair{domain, "data/v210_flow.json", V210_FLOW_ID, TEST_BACKEND_OPTIONS};
    auto const flowDir = mxl::lib::makeFlowDirectoryName(domain, V210_FLOW_ID);
    auto const setVersion = [](std::filesystem::path const& path, char const* version)
    {
        auto const content = std::regex_replace(
            mxl::tests::readFile(path), std::regex{R"("version"\s*:\s*[0-9.]+)"}, std::string{"\"version\": "} + version);
        auto out = std::ofstream{path, std::ios::out | std::ios::trunc};
        out << content;
    };

    auto instance = mxlCreateInstance(domain.string().c_str(), "");
    REQUIRE(instance != nullptr);
    mxlFlowReader reader = nullptr;

    // libmxl refuses a payload.json it cannot read.
    setVersion(flowDir / "payload.json", "2");
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, "", &reader) == MXL_ERR_UNSUPPORTED_OPERATION);
    setVersion(flowDir / "payload.json", "1");

    // The backend refuses its own descriptor when it cannot read it.
    setVersion(flowDir / "payload.test.json", "2");
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, "", &reader) == MXL_STATUS_OK);
    auto info = mxlFlowInfo{};
    REQUIRE(mxlFlowReaderGetInfo(reader, &info) == MXL_STATUS_OK);
    auto slots = std::vector<mxlGrainStorage>(info.config.discrete.grainCount);
    REQUIRE(mxlFlowReaderMapSlots(reader, info.config.discrete.grainCount, slots.data()) == MXL_ERR_UNSUPPORTED_OPERATION);

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    mxlDestroyInstance(instance);
}
