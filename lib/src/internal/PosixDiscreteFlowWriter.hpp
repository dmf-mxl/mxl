#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "DiscreteFlowWriter.hpp"
#include "DiscreteFlowData.hpp"

namespace mxl::lib
{
    class FlowManager;

    ///
    /// Implementation of a FlowWriter based on POSIX shared memory mapping.
    ///
    class PosixDiscreteFlowWriter
        : public DiscreteFlowWriter
    {
    public:
        ///
        /// Creates a PosixDiscreteFlowWriter
        ///
        /// \param[in] manager A referene to the flow manager used to obtain
        ///         additional information about the flows context.
        ///
        PosixDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data);

        ///
        /// \see FlowWriter::getFlowInfo
        ///
        virtual FlowInfo getFlowInfo() override;

        ///
        /// \see DiscreteFlowWriter::openGrain
        ///
        virtual mxlStatus openGrain(std::uint64_t in_index, GrainInfo* out_grainInfo, std::uint8_t** out_payload) override;

        ///
        /// \see DiscreteFlowWriter::commit
        ///
        virtual mxlStatus commit(GrainInfo const& grainInfo) override;

        ///
        /// \see DiscreteFlowWriter::cancel
        ///
        virtual mxlStatus cancel() override;

        ///
        /// \see FlowWriter
        ///
        virtual void flowRead() override;

    private:
        /// The FlowData for the currently opened flow. null if no flow is opened.
        std::unique_ptr<DiscreteFlowData> _flowData;
        /// The currently opened grain index. MXL_UNDEFINED_INDEX if no grain is currently opened.
        std::uint64_t _currentIndex;
    };
}