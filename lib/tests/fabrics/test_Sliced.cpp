// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <catch.hpp>
#include <unistd.h>
#include <fmt/base.h>
#include <mxl/fabrics.h>
#include "mxl/flow.h"

namespace
{
    constexpr auto TEST_OP_TIMEOUT = std::chrono::seconds(5);

    template<typename F, typename... Args>
    void check(F f, std::string const& msg, Args&&... args)
    {
        auto const ret = f(std::forward<Args>(args)...);
        if (ret != MXL_STATUS_OK)
        {
            throw std::runtime_error{msg + " (status " + std::to_string(ret) + ")"};
        }
    }

    std::string readFile(std::filesystem::path const& filepath)
    {
        if (auto file = std::ifstream{filepath, std::ios::in | std::ios::binary}; file)
        {
            auto reader = std::stringstream{};
            reader << file.rdbuf();
            return reader.str();
        }
        throw std::runtime_error("Failed to open file: " + filepath.string());
    }

    struct ProviderTCP
    {
        constexpr static auto provider = MXL_FABRICS_PROVIDER_TCP;
        constexpr static char const* targetNode = "127.0.0.1";
        constexpr static char const* targetService = "0";
        constexpr static char const* initiatorNode = "127.0.0.1";
        constexpr static char const* initiatorService = "0";
    };

    struct ProviderSHM
    {
        constexpr static auto provider = MXL_FABRICS_PROVIDER_SHM;
        constexpr static char const* targetNode = "target";
        constexpr static char const* targetService = "sliced";
        constexpr static char const* initiatorNode = "initiator";
        constexpr static char const* initiatorService = "sliced";
    };

    struct FlowV210
    {
        constexpr static char const* flowDefPath = "../data/v210_flow.json";
        constexpr static char const* flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
        constexpr static auto hasAlphaChannel = false;
    };

    struct FlowV210a
    {
        constexpr static char const* flowDefPath = "../data/v210a_flow.json";
        constexpr static char const* flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
        constexpr static auto hasAlphaChannel = true;
    };

    template<typename Provider, typename Flow>
    struct TestVariant
        : Provider
        , Flow
    {};

    using TCP_V210 = TestVariant<ProviderTCP, FlowV210>;
    using TCP_V210a = TestVariant<ProviderTCP, FlowV210a>;
    using SHM_V210 = TestVariant<ProviderSHM, FlowV210>;
    using SHM_V210a = TestVariant<ProviderSHM, FlowV210a>;

    class TempDomainGuard
    {
    public:
        TempDomainGuard()
            : _path()
        {
            char path[] = "/dev/shm/mxl-domain-XXXXXX";
            if (::mkdtemp(path) == nullptr)
            {
                throw std::runtime_error{"failed to create temporary domain"};
            }

            _path = path;
        }

        ~TempDomainGuard()
        {
            std::filesystem::remove_all(_path);
        }

        char const* c_str() const
        {
            return _path.c_str();
        }

    private:
        std::string _path;
    };

    template<typename ProviderType>
    class FabricsTestFixture
    {
    public:
        FabricsTestFixture()
            : _targetDomain()
            , _initiatorDomain()
        {
            _initiatorInstance = mxlCreateInstance(_initiatorDomain.c_str(), nullptr);
            if (_initiatorInstance == nullptr)
            {
                throw std::runtime_error{"failed to create initiator mxl instance"};
            }
            _targetInstance = mxlCreateInstance(_targetDomain.c_str(), nullptr);
            if (_targetInstance == nullptr)
            {
                throw std::runtime_error{"failed to create target mxl instance"};
            }

            auto const flowDef = readFile(ProviderType::flowDefPath);
            auto const* flowId = ProviderType::flowId;

            // clang-format off
            check(mxlFabricsCreateInstance, "failed to create initiator fabrics instance",
                _initiatorInstance, nullptr, &_initiatorFabricsInstance);
            check(mxlFabricsCreateInstance, "failed to create target fabrics instance",
                _targetInstance, nullptr, &_targetFabricsInstance);
            check(mxlCreateFlowWriter, "failed to create initiator flow writer",
                _initiatorInstance, flowDef.c_str(), nullptr, &_initiatorWriter, &_flowConfigInfo, nullptr);
            check(mxlCreateFlowReader, "failed to create initiator flow reader",
                _initiatorInstance, flowId, nullptr, &_initiatorReader);
            check(mxlCreateFlowWriter, "failed to create target flow writer",
                _targetInstance, flowDef.c_str(), nullptr, &_targetWriter, nullptr, nullptr);
            check(mxlCreateFlowReader, "failed to create target flow reader",
                _targetInstance, flowId, nullptr, &_targetReader);
            check(mxlFabricsCreateTarget, "failed to create fabrics target",
                _targetFabricsInstance, &_target);
            check(mxlFabricsCreateInitiator, "failed to create fabrics initiator",
                _initiatorFabricsInstance, &_initiator);

            auto targetConfig = mxlFabricsTargetConfig{
                .version = MXL_FABRICS_API_VERSION,
                .interface = {
                    .version = MXL_FABRICS_API_VERSION,
                    .provider = ProviderType::provider,
                    .caps = {},
                    .address = {.node = ProviderType::targetNode, .service = ProviderType::targetService},
                    .attr = nullptr,
                },
                .writer = _targetWriter,
            };
            check(mxlFabricsTargetSetup, "failed to set up target",
                _target, &targetConfig, nullptr, &_targetInfo);

            auto initiatorConfig = mxlFabricsInitiatorConfig{
                .version = MXL_FABRICS_API_VERSION,
                .interface = {
                    .version = MXL_FABRICS_API_VERSION,
                    .provider = ProviderType::provider,
                    .caps = {},
                    .address = {.node = ProviderType::initiatorNode, .service = ProviderType::initiatorService},
                    .attr = nullptr,
                },
                .reader = _initiatorReader,
            };

            check(mxlFabricsInitiatorSetup, "failed to set up initiator",
                _initiator, &initiatorConfig, nullptr);
            check(mxlFabricsInitiatorAddTarget, "failed to add target to initiator",
                _initiator, _targetInfo);
            // clang-format on

            driveConnectionProgress();
        }

        virtual ~FabricsTestFixture()
        {
            mxlFabricsDestroyInitiator(_initiatorFabricsInstance, _initiator);
            mxlFabricsDestroyTarget(_targetFabricsInstance, _target);
            mxlFabricsFreeTargetInfo(_targetInfo);

            mxlReleaseFlowReader(_targetInstance, _targetReader);
            mxlReleaseFlowWriter(_targetInstance, _targetWriter);
            mxlReleaseFlowReader(_initiatorInstance, _initiatorReader);
            mxlReleaseFlowWriter(_initiatorInstance, _initiatorWriter);

            mxlFabricsDestroyInstance(_initiatorFabricsInstance);
            mxlFabricsDestroyInstance(_targetFabricsInstance);
            mxlDestroyInstance(_targetInstance);
            mxlDestroyInstance(_initiatorInstance);
        }

    protected:
        bool hasAlphaChannel() const noexcept
        {
            return ProviderType::hasAlphaChannel;
        }

        // Fill all planes of the initiator grain with zero
        void fillInitiatorGrainWithZeros(std::uint64_t index)
        {
            fillGrainWithZeros(_initiatorWriter, index);
        }

        // Fill all planes of the target grain with zero
        void fillTargetGrainWithZeros(std::uint64_t index)
        {
            fillGrainWithZeros(_targetWriter, index);
        }

        // Get the grain header on the targe side
        mxlGrainInfo getTargetGrainInfo(std::uint64_t index)
        {
            auto payload = std::add_pointer_t<std::uint8_t>{nullptr};
            auto grainInfo = mxlGrainInfo{};
            REQUIRE(mxlFlowReaderGetGrainNonBlocking(_targetReader, index, &grainInfo, &payload) == MXL_STATUS_OK);
            return grainInfo;
        }

        // Get the grain header on the initiator side
        mxlGrainInfo getInitiatorGrainInfo(std::uint64_t index)
        {
            auto payload = std::add_pointer_t<std::uint8_t>{nullptr};
            auto grainInfo = mxlGrainInfo{};
            REQUIRE(mxlFlowReaderGetGrainNonBlocking(_initiatorReader, index, &grainInfo, &payload) == MXL_STATUS_OK);
            return grainInfo;
        }

        // Write a slice of key and fill data on the initiator grain
        void writeInitiatorGrainSlice(std::uint64_t index, std::uint32_t sliceIndex, std::uint8_t fill, std::uint8_t key)
        {
            auto grainInfo = mxlGrainInfo{};
            auto payload = static_cast<std::uint8_t*>(nullptr);
            REQUIRE(mxlFlowWriterOpenGrain(_initiatorWriter, index, &grainInfo, &payload) == MXL_STATUS_OK);

            auto planeOffset = std::uint32_t{0};
            auto planeIndex = std::size_t{0};
            for (auto const sliceLen : _flowConfigInfo.discrete.sliceSizes)
            {
                if (sliceLen == 0)
                {
                    break;
                }

                auto sliceStart = payload + planeOffset + (static_cast<std::size_t>(sliceLen) * sliceIndex);
                std::memset(sliceStart, planeIndex == 0 ? fill : key, sliceLen);
                planeOffset += static_cast<std::size_t>(sliceLen) * grainInfo.totalSlices;
                ++planeIndex;
            }

            grainInfo.validSlices = static_cast<std::uint16_t>(sliceIndex + 1);
            REQUIRE(mxlFlowWriterCommitGrain(_initiatorWriter, &grainInfo) == MXL_STATUS_OK);
        }

        // Run a full grain slice range transfer
        std::uint64_t transferGrainSlices(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice)
        {
            auto deadline = std::chrono::steady_clock::now() + TEST_OP_TIMEOUT;
            auto initiatorDone = false;
            auto targetDone = false;
            auto readIndex = std::uint64_t{0};
            auto status = MXL_STATUS_OK;

            // Enqueue the transfer until successfull
            // The SHM provider needs the progress and read calls even if no transfers have been queued yet to become ready.
            do
            {
                status = mxlFabricsInitiatorTransferGrain(_initiator, grainIndex, startSlice, endSlice);
                mxlFabricsInitiatorMakeProgressNonBlocking(_initiator); // the SHM provider needs a CQ read here sometimes to not get stuck
                if (mxlFabricsTargetReadGrainNonBlocking(_target, &readIndex) == MXL_STATUS_OK)
                {
                    // The SHM provider is sometimes ready right after the write has been enqueued.
                    return readIndex;
                }
            }
            while (status != MXL_STATUS_OK && std::chrono::steady_clock::now() < deadline);

            // Timeout of not ok
            REQUIRE(status == MXL_STATUS_OK);

            // Drive progress on both sides of the connection until the grain slices have been transferred
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (!initiatorDone)
                {
                    auto status = mxlFabricsInitiatorMakeProgressNonBlocking(_initiator);
                    if (status == MXL_STATUS_OK)
                    {
                        initiatorDone = true;
                    }
                    else
                    {
                        REQUIRE(status == MXL_ERR_NOT_READY);
                    }
                }

                if (!targetDone)
                {
                    auto status = mxlFabricsTargetReadGrainNonBlocking(_target, &readIndex);
                    if (status == MXL_STATUS_OK)
                    {
                        targetDone = true;
                    }
                    else
                    {
                        REQUIRE(status == MXL_ERR_NOT_READY);
                    }
                }

                if (initiatorDone && targetDone)
                {
                    return readIndex;
                }
            }

            FAIL("grain slice transfer did not complete within timeout");
            return readIndex;
        }

        // Read the first byte of a slice in the key and fill buffer
        std::pair<std::uint8_t, std::uint8_t> readTargetGrainSlice(std::uint64_t index, std::uint32_t sliceIndex)
        {
            auto grainInfo = mxlGrainInfo{};
            auto payload = static_cast<std::uint8_t*>(nullptr);
            REQUIRE(mxlFlowReaderGetGrainSliceNonBlocking(_targetReader, index, static_cast<std::uint16_t>(sliceIndex + 1), &grainInfo, &payload) ==
                    MXL_STATUS_OK);

            auto result = std::pair<std::uint8_t, std::uint8_t>{0, 0};
            auto planeOffset = std::uint32_t{0};
            auto planeIndex = std::size_t{0};
            for (auto const sliceLen : _flowConfigInfo.discrete.sliceSizes)
            {
                if (sliceLen == 0)
                {
                    break;
                }

                auto byte = *(payload + (planeOffset + static_cast<std::size_t>(sliceLen) * sliceIndex));
                if (planeIndex == 0)
                {
                    result.first = byte;
                }
                else
                {
                    result.second = byte;
                }

                planeOffset += static_cast<std::size_t>(sliceLen) * grainInfo.totalSlices;
                ++planeIndex;
            }

            return result;
        }

    private:
        // fill a grain at "index" with zero
        void fillGrainWithZeros(mxlFlowWriter writer, std::uint64_t index)
        {
            auto grainInfo = mxlGrainInfo{};
            auto payload = static_cast<std::uint8_t*>(nullptr);
            REQUIRE(mxlFlowWriterOpenGrain(writer, index, &grainInfo, &payload) == MXL_STATUS_OK);

            auto planeOffset = std::uint32_t{0};
            for (auto const sliceLen : _flowConfigInfo.discrete.sliceSizes)
            {
                if (sliceLen == 0)
                {
                    break;
                }

                std::memset(payload + planeOffset, 0, static_cast<std::size_t>(sliceLen) * grainInfo.totalSlices);
                planeOffset += static_cast<std::size_t>(sliceLen) * grainInfo.totalSlices;
            }

            grainInfo.validSlices = 0;
            grainInfo.flags = MXL_GRAIN_FLAG_INVALID;
            REQUIRE(mxlFlowWriterCommitGrain(writer, &grainInfo) == MXL_STATUS_OK);
        }

        // drive progress for the connection to be established
        void driveConnectionProgress()
        {
            auto deadline = std::chrono::steady_clock::now() + TEST_OP_TIMEOUT;
            while (std::chrono::steady_clock::now() < deadline)
            {
                auto dummyIndex = std::uint64_t{0};
                mxlFabricsTargetReadGrainNonBlocking(_target, &dummyIndex);

                auto status = mxlFabricsInitiatorMakeProgressNonBlocking(_initiator);
                if (status == MXL_STATUS_OK)
                {
                    return;
                }
                if (status != MXL_ERR_NOT_READY)
                {
                    throw std::runtime_error{"initiator progress failed with status " + std::to_string(status)};
                }
            }
            throw std::runtime_error{"failed to establish connection within timeout"};
        }

    private:
        TempDomainGuard _targetDomain;
        TempDomainGuard _initiatorDomain;

        mxlInstance _initiatorInstance = nullptr;
        mxlInstance _targetInstance = nullptr;
        mxlFabricsInstance _initiatorFabricsInstance = nullptr;
        mxlFabricsInstance _targetFabricsInstance = nullptr;
        mxlFabricsTargetInfo _targetInfo = nullptr;

        mxlFlowConfigInfo _flowConfigInfo = {};

        mxlFlowWriter _initiatorWriter = nullptr;
        mxlFlowReader _initiatorReader = nullptr;
        mxlFlowWriter _targetWriter = nullptr;
        mxlFlowReader _targetReader = nullptr;

        mxlFabricsTarget _target = nullptr;
        mxlFabricsInitiator _initiator = nullptr;
    };
}

TEMPLATE_TEST_CASE_METHOD(FabricsTestFixture, "Slice transfer single", "[sliced][single-slices]", TCP_V210, TCP_V210a, SHM_V210a)
{
    constexpr auto const startGrainIndex = std::uint64_t{140};
    for (auto iteration = std::size_t{0}; iteration < 4; ++iteration)
    {
        auto lastSlice = std::uint16_t{0};
        auto const grainIndex = startGrainIndex + iteration;
        this->fillInitiatorGrainWithZeros(grainIndex);
        this->fillTargetGrainWithZeros(grainIndex);

        auto info = this->getInitiatorGrainInfo(grainIndex);
        for (auto slice = std::uint16_t{0}; slice < info.totalSlices; ++slice)
        {
            // check that the target slice is zeroed
            auto [fillBefore, keyBefore] = this->readTargetGrainSlice(grainIndex, slice);
            REQUIRE(fillBefore == 0x00);
            if (this->hasAlphaChannel())
            {
                REQUIRE(keyBefore == 0x00);
            }

            auto fillValue = static_cast<std::uint8_t>(0xAC & ~slice);
            auto keyValue = static_cast<std::uint8_t>(0xAB & ~slice);

            // write pattern to initiator grain buffer
            this->writeInitiatorGrainSlice(grainIndex, slice, fillValue, keyValue);
            REQUIRE(this->transferGrainSlices(grainIndex, lastSlice, slice + 1) == grainIndex);
            this->readTargetGrainSlice(grainIndex, slice);

            // read after transfer
            auto [fillAfter, keyAfter] = this->readTargetGrainSlice(grainIndex, slice);
            REQUIRE(fillAfter == fillValue);
            if (this->hasAlphaChannel())
            {
                REQUIRE(keyAfter == keyValue);
            }

            // check that validSlices has been updated
            auto const targetGrainInfo = this->getTargetGrainInfo(grainIndex);
            REQUIRE(targetGrainInfo.validSlices == slice + 1);
        }
    }
}

TEMPLATE_TEST_CASE_METHOD(FabricsTestFixture, "Slice transfer blocks", "[sliced][slice-blocks]", TCP_V210, TCP_V210a, SHM_V210)
{
    // test different block sizes
    constexpr auto const blockSizes = std::array{2, 13, 73, 720};
    for (auto const blockSize : blockSizes)
    {
        auto const startGrainIndex = std::uint64_t{140 + static_cast<std::uint64_t>(blockSize)};
        for (auto iteration = std::size_t{0}; iteration < 4; ++iteration)
        {
            auto lastSlice = std::uint16_t{0};
            auto const grainIndex = startGrainIndex + iteration;
            this->fillInitiatorGrainWithZeros(grainIndex);
            this->fillTargetGrainWithZeros(grainIndex);

            auto startSlice = std::uint16_t{0};
            auto info = this->getInitiatorGrainInfo(grainIndex);
            for (;;)
            {
                auto endSlice = startSlice + blockSize;
                if (endSlice >= info.totalSlices)
                {
                    endSlice = info.totalSlices;
                }

                // find a slice in the middle of the block <= (endSlice - 1)
                auto middleSlice = std::min(startSlice + (blockSize / 2), endSlice - 1);
                auto [fillValueBefore, keyValueBefore] = this->readTargetGrainSlice(grainIndex, middleSlice);
                REQUIRE(fillValueBefore == 0x00);
                if (this->hasAlphaChannel())
                {
                    REQUIRE(keyValueBefore == 0x00);
                }

                // generate a value for fill and key buffer
                auto fillValue = static_cast<std::uint8_t>(0xAC & ~middleSlice);
                auto keyValue = static_cast<std::uint8_t>(0xAB & ~middleSlice);

                // write to a slice in the middle of the block
                this->writeInitiatorGrainSlice(grainIndex, middleSlice, fillValue, keyValue);

                // transfer the slices
                REQUIRE(this->transferGrainSlices(grainIndex, lastSlice, endSlice) == grainIndex);

                // check that the payload is correct
                auto [fillValueAfter, keyValueAfter] = this->readTargetGrainSlice(grainIndex, middleSlice);
                REQUIRE(fillValueAfter == fillValue);
                if (this->hasAlphaChannel())
                {
                    REQUIRE(keyValueAfter == keyValue);
                }

                // check that validSlices was updated
                auto const targetGrainInfo = this->getTargetGrainInfo(grainIndex);
                REQUIRE(targetGrainInfo.validSlices == endSlice);

                startSlice += blockSize;
                if (startSlice >= info.totalSlices)
                {
                    break;
                }
            }
        }
    }
}
