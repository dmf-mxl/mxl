// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <limits>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "Utils.hpp"

/*
  This test verifies that a read request for the ring buffer's
  tailIndex grain returns TOO_LATE when the writer has a write
  operation open at headIndex+1.

  +-----------+-- --+-------------+-----------+-------------+--
  | index  0  |     | index N-2   | index N-1 |  index 0    |
  | tailIndex | ... | headIndex-1 | headIndex | headIndex+1 | ...
  +-----------+-- --+-------------+-----------+-------------+--
                                              ^
                                              ring wrap

   N = grainCount

   The diagram illustrates that tailIndex and headIndex+1 address the
   same ring-buffer index.

   Note: index 0 is used here for illustration. The actual ring-buffer
   starting index is tailIndex % grainCount.
*/
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow : tail read during open write", "[mxl flows]")
{
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");

    // Init the domain instance.
    auto instance = mxlCreateInstance(domain.string().c_str(), "");
    REQUIRE(instance != nullptr);

    // Init a flow writer.
    auto writer = mxlFlowWriter{};
    auto configInfo = mxlFlowConfigInfo{};
    auto flowWasCreated = false;

    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), "", &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    // Init headIndex from the current MXL time.
    auto const now = mxlGetTime();
    auto headIndex = mxlTimestampToIndex(&configInfo.common.grainRate, now);
    REQUIRE(headIndex != MXL_UNDEFINED_INDEX);

    auto const grainCount = static_cast<std::uint64_t>(configInfo.discrete.grainCount);

    REQUIRE(grainCount > 1U);
    REQUIRE(headIndex > grainCount);

    // Init each grain in the ring buffer.
    auto tailIndex = headIndex - grainCount + 1;
    for (auto i = std::uint64_t{0}; i < grainCount; ++i)
    {
        auto grainInfo = mxlGrainInfo{};
        std::uint8_t* buffer = nullptr;

        REQUIRE(mxlFlowWriterOpenGrain(writer, tailIndex + i, &grainInfo, &buffer) == MXL_STATUS_OK);

        buffer[0] = 0xCA;
        buffer[grainInfo.grainSize - 1] = 0xFE;
        grainInfo.validSlices = grainInfo.totalSlices;

        REQUIRE(mxlFlowWriterCommitGrain(writer, &grainInfo) == MXL_STATUS_OK);
    }

    // Init a reader and sanity check the flow's headIndex.
    auto reader = mxlFlowReader{};
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    auto runtimeInfo = mxlFlowRuntimeInfo{};
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo.headIndex == headIndex);

    // Reading the tailIndex grain should be TOO_LATE when write is
    // not in progress.
    auto readGrainInfo = mxlGrainInfo{};
    std::uint8_t* readBuffer = nullptr;
    auto const grainReadStatusOutsideWrite = mxlFlowReaderGetGrainNonBlocking(reader, tailIndex, &readGrainInfo, &readBuffer);
    {
        INFO("The tailIndex grain read should return TOO_LATE even when no write "
             "is in progress because the tail grain is reserved for the writer.");
        CHECK(grainReadStatusOutsideWrite == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    }

    // Reading the tailIndex grain through the slice API should be
    // TOO_LATE when write is not in progress.
    readBuffer = nullptr;
    readGrainInfo = {};
    auto const sliceReadStatusOutsideWrite = mxlFlowReaderGetGrainSliceNonBlocking(
        reader, tailIndex, MXL_GRAIN_VALID_SLICES_ANY, &readGrainInfo, &readBuffer);
    {
        INFO("The tailIndex slice read should return TOO_LATE even when no write "
             "is in progress because the tail grain is reserved for the writer.");
        CHECK(sliceReadStatusOutsideWrite == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    }

    // Open a write to the next sequential grain at headIndex+1. This
    // grain shares a ring-buffer position with the tailIndex grain.
    auto writeGrainInfo = mxlGrainInfo{};
    std::uint8_t* writeBuffer = nullptr;
    {
        REQUIRE(mxlFlowWriterOpenGrain(writer, headIndex + 1, &writeGrainInfo, &writeBuffer) == MXL_STATUS_OK);

        INFO("The newly opened grain should identify headIndex+1.");
        CAPTURE(writeGrainInfo.index, headIndex + 1, writeGrainInfo.validSlices, writeGrainInfo.totalSlices);
        CHECK(writeGrainInfo.index == headIndex + 1);
    }

    // Sanity check: verify that headIndex has not yet moved.
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo.headIndex == headIndex);

    // Reading a grain at tailIndex during an open write of
    // headIndex+1 should return TOO_LATE to prevent reader/writer
    // conflict during the writer's open/commit interval.
    constexpr auto NOT_CHANGED_INDEX_SENTINEL_VALUE = std::numeric_limits<decltype(readGrainInfo.index)>::max();
    readBuffer = nullptr;
    readGrainInfo = {};
    readGrainInfo.index = NOT_CHANGED_INDEX_SENTINEL_VALUE;
    auto const grainReadStatusInsideWrite = mxlFlowReaderGetGrainNonBlocking(reader, tailIndex, &readGrainInfo, &readBuffer);
    {
        INFO("A tailIndex read should return TOO_LATE while a write of "
             "headIndex+1 is open because both logical grains alias the "
             "same physical ring-buffer position."

        );
        CHECK(grainReadStatusInsideWrite == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    }
    {
        INFO("Because the tailIndex grain read is expected to return TOO_LATE, "
             "it should not return a payload or modify the grain index.");
        CHECK(readBuffer == nullptr);
        CHECK(readGrainInfo.index == NOT_CHANGED_INDEX_SENTINEL_VALUE);
    }

    // REVIEW-ONLY (pre-fix diagnostic): Retained so running this test
    // against the old implementation also exposes the inconsistent
    // state returned when MXL_STATUS_OK is returned.
    if (grainReadStatusInsideWrite == MXL_STATUS_OK)
    {
        INFO("The tailIndex read was permitted, therefore the returned grain "
             "metadata must be internally consistent: the payload must be "
             "available, the grain index must identify the requested tailIndex "
             "grain, and the full-grain read must report all slices as valid.");
        CAPTURE(readBuffer, readGrainInfo.index, tailIndex, headIndex + 1, readGrainInfo.validSlices, readGrainInfo.totalSlices);
        CHECK(readBuffer != nullptr);
        CHECK(readGrainInfo.validSlices == readGrainInfo.totalSlices);

        // REVIEW-ONLY: Fails with the pre-fix read boundary.  The
        // returned grain is headIndex+1 although tailIndex was
        // requested.
        CHECK(readGrainInfo.index == tailIndex);
    }

    // Reading tailIndex through the slice API during an open write of
    // headIndex+1 should return TOO_LATE, should not return a payload,
    // and should not modify readGrainInfo.validSlices.
    constexpr auto NOT_CHANGED_SLICE_SENTINEL_VALUE = std::numeric_limits<decltype(readGrainInfo.validSlices)>::max();
    readBuffer = nullptr;
    readGrainInfo = {};
    readGrainInfo.validSlices = NOT_CHANGED_SLICE_SENTINEL_VALUE;
    auto const sliceReadStatus = mxlFlowReaderGetGrainSliceNonBlocking(reader, tailIndex, MXL_GRAIN_VALID_SLICES_ANY, &readGrainInfo, &readBuffer);
    {
        INFO("A tailIndex slice read should return TOO_LATE while a write of "
             "headIndex+1 is open because both logical grains alias the "
             "same physical ring-buffer position.");
        CHECK(sliceReadStatus == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    }
    {
        INFO("Because the tailIndex slice read is expected to return TOO_LATE, "
             "it should not return a payload or modify validSlices.");
        CHECK(readBuffer == nullptr);
        CHECK(readGrainInfo.validSlices == NOT_CHANGED_SLICE_SENTINEL_VALUE);
    }

    // REVIEW-ONLY (pre-fix diagnostic): Retained so running this test
    // against the old implementation also exposes the inconsistent
    // state returned when MXL_STATUS_OK is returned.
    if (sliceReadStatus == MXL_STATUS_OK)
    {
        INFO("The tailIndex slice read was permitted, therefore the returned "
             "grain state must be internally consistent: the payload must be "
             "available, the grain index must identify the requested tailIndex "
             "grain, and the previously committed tail grain must still be "
             "reported as fully valid.");

        CAPTURE(readBuffer, readGrainInfo.index, tailIndex, headIndex + 1, readGrainInfo.validSlices, readGrainInfo.totalSlices);
        CHECK(readBuffer != nullptr);
        CHECK(readGrainInfo.validSlices == readGrainInfo.totalSlices);

        // REVIEW-ONLY: Fails with the pre-fix read boundary.  The
        // returned grain is headIndex+1 although tailIndex was
        // requested.
        CHECK(readGrainInfo.index == tailIndex);
    }

    // Commit the write and update local headIndex.
    writeBuffer[0] = 0xCA;
    writeBuffer[writeGrainInfo.grainSize - 1] = 0xFE;
    writeGrainInfo.validSlices = writeGrainInfo.totalSlices;
    REQUIRE(mxlFlowWriterCommitGrain(writer, &writeGrainInfo) == MXL_STATUS_OK);

    headIndex = headIndex + 1;
    tailIndex = headIndex - grainCount + 1;

    // Sanity check local headIndex matches flow's headIndex.
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo.headIndex == headIndex);

    // Post commit read of the tailIndex grain should continue to be
    // TOO_LATE.
    readBuffer = nullptr;
    readGrainInfo = {};
    auto const grainReadStatusPostCommit = mxlFlowReaderGetGrainNonBlocking(reader, tailIndex, &readGrainInfo, &readBuffer);
    {
        INFO("A post commit tailIndex read should return TOO_LATE after a "
             "write of headIndex+1 is committed because both logical grains "
             "alias the same physical ring-buffer position.");
        CHECK(grainReadStatusPostCommit == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    }

    // Tear down.
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);

    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);

    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

/*
  This test verifies that the audio reader boundary between the
  writer's half-ring and the reader's half-ring is enforced regardless
  of whether the writer has a write operation open.

  +--------------------------+--------------------------+--------------------------+--
  | physical half 0          | physical half 1          | physical half 0          |
  | writer's half-ring       | reader's half-ring       | writer's half-ring       | ...
  +--------------------------+--------------------------+--------------------------+--
  ^                          ^                          ^
  ringTailIndex              boundaryIndex              headIndex
                                                        ring wrap

  ringTailIndex = headIndex - bufferLength + 1
  boundaryIndex = headIndex - bufferLength/2 + 1

  Reads at boundaryIndex are allowed. Reads at boundaryIndex-1 and
  earlier are TOO_LATE.

  Note: physical half 0/1 are used here to illustrate the two
  half-ring regions and their reuse after ring wrap.
*/
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Audio Flow : tail read during open write", "[mxl flows]")
{
    auto const flowId = "b3bb5be7-9fe9-4324-a5bb-4c70e1084449";
    auto const flowDef = mxl::tests::readFile("data/audio_flow.json");

    // Init the domain instance.
    auto instance = mxlCreateInstance(domain.string().c_str(), "");
    REQUIRE(instance != nullptr);

    // Init a flow writer.
    auto writer = mxlFlowWriter{};
    auto configInfo = mxlFlowConfigInfo{};
    auto flowWasCreated = false;

    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), "", &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    // Init headIndex from the current MXL time.
    auto const now = mxlGetTime();
    auto headIndex = mxlTimestampToIndex(&configInfo.common.grainRate, now);
    REQUIRE(headIndex != MXL_UNDEFINED_INDEX);

    auto const bufferLength = static_cast<std::uint64_t>(configInfo.continuous.bufferLength);
    auto const halfBufferLength = bufferLength / 2U;

    REQUIRE(halfBufferLength > 0U);
    REQUIRE(headIndex > bufferLength);

    // Init the full ring buffer using two sequential half-buffer
    // writes ending at headIndex. Each write respects the writer's
    // half-ring limit.
    {
        auto const boundaryIndex = headIndex - halfBufferLength + 1;
        auto tmpWriteIndex = boundaryIndex - 1;
        for (auto i = std::uint64_t{0}; i < 2U; ++i)
        {
            auto writeBuffer = mxlMutableWrappedMultiBufferSlice{};

            REQUIRE(mxlFlowWriterOpenSamples(writer, tmpWriteIndex, halfBufferLength, &writeBuffer) == MXL_STATUS_OK);

            REQUIRE(mxlFlowWriterCommitSamples(writer) == MXL_STATUS_OK);

            tmpWriteIndex += halfBufferLength;
        }
    }

    // Init a reader and sanity check the flow's headIndex.
    auto reader = mxlFlowReader{};
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    auto runtimeInfo = mxlFlowRuntimeInfo{};
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo.headIndex == headIndex);

    // boundaryIndex separates the writer's half-ring from the reader's
    // half-ring.
    auto const boundaryIndex = headIndex - halfBufferLength + 1;
    auto const ringTailIndex = headIndex - bufferLength + 1;

    auto readBuffer = mxlWrappedMultiBufferSlice{};

    // Reading at boundaryIndex is OK.
    REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, boundaryIndex, 1U, &readBuffer) == MXL_STATUS_OK);

    // Reading one index before boundaryIndex is TOO_LATE.
    readBuffer = {};
    REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, boundaryIndex - 1, 1U, &readBuffer) == MXL_ERR_OUT_OF_RANGE_TOO_LATE);

    // Open a write to the next sequential half-buffer at
    // headIndex+halfBufferLength. This does *not* overlap the
    // reader's half-ring.
    auto writeBuffer = mxlMutableWrappedMultiBufferSlice{};
    REQUIRE(mxlFlowWriterOpenSamples(writer, headIndex + halfBufferLength, halfBufferLength, &writeBuffer) == MXL_STATUS_OK);

    // Sanity check: verify that headIndex has not yet moved.
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo.headIndex == headIndex);

    // The reader boundary is unchanged while the write is open.
    readBuffer = {};
    REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, boundaryIndex, 1U, &readBuffer) == MXL_STATUS_OK);

    // A read at the tail is also in the writer's half-ring and should
    // fail with TOO_LATE.
    readBuffer = {};
    REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, ringTailIndex, 1U, &readBuffer) == MXL_ERR_OUT_OF_RANGE_TOO_LATE);

    // A read just before boundaryIndex should also fail with
    // TOO_LATE.
    readBuffer = {};
    REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, boundaryIndex - 1, 1U, &readBuffer) == MXL_ERR_OUT_OF_RANGE_TOO_LATE);

    // Commit the write and update local headIndex.
    REQUIRE(mxlFlowWriterCommitSamples(writer) == MXL_STATUS_OK);

    headIndex = headIndex + halfBufferLength;

    // Sanity check that the local headIndex matches the flow's
    // headIndex.
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo.headIndex == headIndex);

    // Post commit read at the new boundaryIndex is OK.
    auto const newBoundaryIndex = headIndex - halfBufferLength + 1;
    readBuffer = {};

    REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, newBoundaryIndex, 1U, &readBuffer) == MXL_STATUS_OK);

    // Tear down.
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);

    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);

    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}
