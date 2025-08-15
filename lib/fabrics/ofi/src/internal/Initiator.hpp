#pragma once

#include <cstdint>
#include <memory>
#include <uuid.h>
#include "mxl/fabrics.h"
#include "Endpoint.hpp"
#include "RemoteRegion.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    struct InitiatorTargetEntry
    {
        Endpoint endpoint;
        std::vector<RemoteRegionGroup> remoteGroups;
    };

    class Initiator
    {
    public:
        virtual ~Initiator() = default;

        virtual void addTarget(TargetInfo const& targetInfo) = 0;
        virtual void removeTarget(TargetInfo const& targetInfo) = 0;
        virtual void transferGrain(uint64_t grainIndex) = 0;
        virtual bool makeProgress() = 0;
        virtual bool makeProgressBlocking(std::chrono::steady_clock::duration) = 0;
    };

    class InitiatorWrapper
    {
    public:
        static InitiatorWrapper* fromAPI(mxlFabricsInitiator api) noexcept;
        mxlFabricsInitiator toAPI() noexcept;

        void setup(mxlInitiatorConfig const& config);

        void addTarget(TargetInfo const& targetInfo);
        void removeTarget(TargetInfo const& targetInfo);
        void transferGrain(uint64_t grainIndex);
        bool makeProgress();
        bool makeProgressBlocking(std::chrono::steady_clock::duration);

    private:
        std::unique_ptr<Initiator> _inner;
    };
}
