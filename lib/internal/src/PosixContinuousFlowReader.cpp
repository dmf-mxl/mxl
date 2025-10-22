// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "PosixContinuousFlowReader.hpp"
#include <sys/stat.h>
#include "mxl-internal/PathUtils.hpp"

namespace mxl::lib
{
    PosixContinuousFlowReader::PosixContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<ContinuousFlowData>&& data)
        : ContinuousFlowReader{flowId, manager.getDomain()}
        , _flowData{std::move(data)}
        , _channelCount{_flowData->channelCount()}
        , _bufferLength{_flowData->channelBufferLength()}
    {}

    mxlFlowInfo PosixContinuousFlowReader::getFlowInfo()
    {
        if (_flowData)
        {
            return *_flowData->flowInfo();
        }
        throw std::runtime_error("No open flow.");
    }

    FlowData& PosixContinuousFlowReader::getFlowData()
    {
        if (_flowData)
        {
            return *_flowData;
        }

        throw std::runtime_error("No open flow.");
    }

    mxlStatus PosixContinuousFlowReader::getSamples(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice& payloadBuffersSlices)
    {
        if (_flowData)
        {
            if (auto const headIndex = _flowData->flowInfo()->continuous.headIndex; index <= headIndex)
            {
                auto const minIndex = (headIndex >= (_bufferLength / 2U)) ? (headIndex - (_bufferLength / 2U)) : std::uint64_t{0};

                if ((index >= minIndex) && ((index - minIndex) >= count))
                {
                    auto const startOffset = (index + _bufferLength - count) % _bufferLength;
                    auto const endOffset = (index % _bufferLength);

                    auto const firstLength = (startOffset < endOffset) ? count : _bufferLength - startOffset;
                    auto const secondLength = count - firstLength;

                    auto const baseBufferPtr = static_cast<std::uint8_t const*>(_flowData->channelData());
                    auto const sampleWordSize = _flowData->sampleWordSize();

                    payloadBuffersSlices.base.fragments[0].pointer = baseBufferPtr + sampleWordSize * startOffset;
                    payloadBuffersSlices.base.fragments[0].size = sampleWordSize * firstLength;

                    payloadBuffersSlices.base.fragments[1].pointer = baseBufferPtr;
                    payloadBuffersSlices.base.fragments[1].size = sampleWordSize * secondLength;

                    payloadBuffersSlices.stride = sampleWordSize * _bufferLength;
                    payloadBuffersSlices.count = _channelCount;

                    return MXL_STATUS_OK;
                }

                // ouf of range. check if the flow is still valid.
                if (!isFlowValid())
                {
                    return MXL_ERR_FLOW_INVALID;
                }
                else
                {
                    return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
                }
            }

            // ouf of range. check if the flow is still valid.
            if (!isFlowValid())
            {
                return MXL_ERR_FLOW_INVALID;
            }
            else
            {
                return MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
            }
        }

        return MXL_ERR_UNKNOWN;
    }

    bool PosixContinuousFlowReader::isFlowValid() const
    {
        if (_flowData)
        {
            auto const flowInfo = _flowData->flowInfo();
            auto const flowDataPath = makeFlowDataFilePath(getDomain(), to_string(getId()));
            struct stat st;
            if (stat(flowDataPath.string().c_str(), &st) != 0)
            {
                return false;
            }
            return (st.st_ino == flowInfo->common.inode);
        }
        return false;
    }
}
