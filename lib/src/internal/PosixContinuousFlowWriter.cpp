// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "PosixContinuousFlowWriter.hpp"
#include <stdexcept>
#include <mxl/time.h>

namespace mxl::lib
{
    PosixContinuousFlowWriter::PosixContinuousFlowWriter(FlowManager const&, uuids::uuid const& flowId, std::unique_ptr<ContinuousFlowData>&& data)
        : ContinuousFlowWriter{flowId}
        , _flowData{std::move(data)}
        , _channelCount{_flowData->channelCount()}
        , _bufferLength{_flowData->channelBufferLength()}
        , _currentIndex{MXL_UNDEFINED_INDEX}
    {}

    FlowInfo PosixContinuousFlowWriter::getFlowInfo()
    {
        if (_flowData)
        {
            return *_flowData->flowInfo();
        }
        throw std::runtime_error("No open flow.");
    }

    FlowData& PosixContinuousFlowWriter::getFlowData()
    {
        if (_flowData)
        {
            return *_flowData;
        }

        throw std::runtime_error("No open flow.");
    }

    mxlStatus PosixContinuousFlowWriter::openSamples(std::uint64_t index, std::size_t count, MutableWrappedMultiBufferSlice& payloadBufferSlices)
    {
        if (_flowData)
        {
            if (count < (_bufferLength / 2))
            {
                auto const startOffset = (index + _bufferLength - count) % _bufferLength;
                auto const endOffset = (index % _bufferLength);

                auto const firstLength = (startOffset < endOffset) ? count : _bufferLength - startOffset;
                auto const secondLength = count - firstLength;

                auto const baseBufferPtr = static_cast<std::uint8_t*>(_flowData->channelData());
                auto const sampleWordSize = _flowData->sampleWordSize();

                payloadBufferSlices.base.fragments[0].pointer = baseBufferPtr + sampleWordSize * startOffset;
                payloadBufferSlices.base.fragments[0].size = sampleWordSize * firstLength;

                payloadBufferSlices.base.fragments[1].pointer = baseBufferPtr + sampleWordSize * endOffset;
                payloadBufferSlices.base.fragments[1].size = sampleWordSize * secondLength;

                payloadBufferSlices.stride = sampleWordSize * _bufferLength;
                payloadBufferSlices.count = _channelCount;

                _currentIndex = index;

                return MXL_STATUS_OK;
            }

            return MXL_ERR_INVALID_ARG;
        }
        return MXL_ERR_UNKNOWN;
    }

    mxlStatus PosixContinuousFlowWriter::commit()
    {
        if (_flowData)
        {
            auto const flowInfo = _flowData->flowInfo();
            flowInfo->continuous.headIndex = _currentIndex;

            _currentIndex = MXL_UNDEFINED_INDEX;

            return MXL_STATUS_OK;
        }
        else
        {
            _currentIndex = MXL_UNDEFINED_INDEX;
            return MXL_ERR_UNKNOWN;
        }
    }

    mxlStatus PosixContinuousFlowWriter::cancel()
    {
        _currentIndex = MXL_UNDEFINED_INDEX;
        return MXL_STATUS_OK;
    }

    void PosixContinuousFlowWriter::flowRead()
    {}
}
