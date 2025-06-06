#include "PosixDiscreteFlowReader.hpp"
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <array>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <sys/stat.h>
#include "Flow.hpp"
#include "FlowManager.hpp"
#include "Logging.hpp"
#include "PathUtils.hpp"
#include "SharedMemory.hpp"
#include "Sync.hpp"

namespace mxl::lib
{
    namespace
    {
        bool updateFileAccessTime(int fd) noexcept
        {
            auto const times = std::array<timespec, 2>{{{0, UTIME_NOW}, {0, UTIME_OMIT}}};
            return (::futimens(fd, times.data()) == 0);
        }
    }

    PosixDiscreteFlowReader::PosixDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data)
        : DiscreteFlowReader{flowId}
        , _flowData{std::move(data)}
        , _accessFileFd{-1}
    {
        auto const accessFile = makeFlowAccessFilePath(manager.getDomain(), to_string(flowId));
        _accessFileFd = ::open(accessFile.string().c_str(), O_RDWR);

        // Opening the access file may fail if the domain is in a read only volume.
        // we can still execute properly but the 'lastReadTime' will never be updated.
        // Ignore failures.
    }

    FlowInfo PosixDiscreteFlowReader::getFlowInfo()
    {
        if (_flowData)
        {
            return *_flowData->flowInfo();
        }
        throw std::runtime_error("No open flow.");
    }

    mxlStatus PosixDiscreteFlowReader::getGrain(std::uint64_t in_index, std::uint64_t in_timeoutNs, GrainInfo* out_grainInfo, std::uint8_t** out_payload)
    {
        auto status = MXL_ERR_TIMEOUT;

        if (_flowData)
        {
            auto const flowInfo = _flowData->flowInfo();
            if (auto const headIndex = flowInfo->discrete.headIndex; in_index <= headIndex)
            {
                auto const grainCount = flowInfo->discrete.grainCount;
                auto const minIndex = (headIndex >= grainCount)
                    ? (headIndex - grainCount + 1U)
                    : std::uint64_t{0};

                if (in_index >= minIndex)
                {
                    auto const offset = in_index % flowInfo->discrete.grainCount;
                    auto const grain = _flowData->grainAt(offset);
                    *out_grainInfo = grain->header.info;
                    *out_payload = reinterpret_cast<std::uint8_t*>(&grain->header + 1);

                    status = MXL_STATUS_OK;
                }
                else
                {
                    status = MXL_ERR_OUT_OF_RANGE_TOO_LATE;
                }
            }
            else
            {
                // FIXME: This code is hopelessly broken. As it assumes that the
                //      next write provides the index of interest, which is not
                //      guaranteed.
                if (waitUntilChanged(&flowInfo->discrete.syncCounter, flowInfo->discrete.syncCounter, Duration(in_timeoutNs)))
                {
                    auto const offset = in_index % flowInfo->discrete.grainCount;
                    auto const grain = _flowData->grainAt(offset);
                    *out_grainInfo = grain->header.info;
                    *out_payload = reinterpret_cast<std::uint8_t*>(&grain->header + 1);

                    status = MXL_STATUS_OK;
                }
                else
                {
                    status = MXL_ERR_TIMEOUT;
                }
            }

            if (status == MXL_STATUS_OK)
            {
                if (!updateFileAccessTime(_accessFileFd))
                {
                    MXL_ERROR("Failed to update access file times");
                }
            }
        }

        return status;
    }

    mxlStatus PosixDiscreteFlowReader::getGrain(std::uint64_t in_index, GrainInfo* out_grainInfo, std::uint8_t** out_payload)
    {
        if (_flowData)
        {
            auto const flowInfo = _flowData->flowInfo();
            if (auto const headIndex = flowInfo->discrete.headIndex; in_index <= headIndex)
            {
                auto const grainCount = flowInfo->discrete.grainCount;
                auto const minIndex = (headIndex >= grainCount)
                    ? (headIndex - grainCount + 1U)
                    : std::uint64_t{0};
                if (in_index >= minIndex)
                {
                    auto const offset = in_index % grainCount;
                    auto const grain = _flowData->grainAt(offset);
                    *out_grainInfo = grain->header.info;
                    *out_payload = reinterpret_cast<std::uint8_t*>(&grain->header + 1);

                    updateFileAccessTime(_accessFileFd);

                    return MXL_STATUS_OK;
                }

                return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
            }

            return MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
        }

        return MXL_ERR_UNKNOWN;
    }

} // namespace mxl::lib
