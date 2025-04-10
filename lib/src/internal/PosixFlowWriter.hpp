#pragma once

#include "FlowManager.hpp"
#include "FlowWriter.hpp"

#include <cstdint>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <uuid.h>

namespace mxl::lib {

///
/// Implementation of a FlowWriter based on POSIX shared memory mapping.
///
class PosixFlowWriter : public FlowWriter
{
public:
    ///
    /// Creates a PosixFlowWriter
    ///
    /// \param in_manager The flow manager
    ///
    PosixFlowWriter( FlowManager::ptr in_manager );

    ///
    /// \see FlowWriter::open
    ///
    virtual bool open( const uuids::uuid &in_id ) override;

    ///
    /// \see FlowWriter::close
    ///
    virtual void close() override;

    ///
    /// \see FlowWriter::getFlowInfo
    ///
    virtual FlowInfo getFlowInfo() override;

    ///
    /// \see FlowWriter::openGrain
    ///
    virtual mxlStatus openGrain( uint64_t in_index, GrainInfo *out_grainInfo, uint8_t **out_payload ) override;

    ///
    /// \see FlowWriter::cancel
    ///
    virtual mxlStatus cancel() override;

    ///
    /// \see FlowWriter::commit
    ///
    virtual mxlStatus commit( const GrainInfo *in_grainInfo ) override;

    ///
    /// \see FlowWriter::incrementSlice
    ///
    virtual mxlStatus incrementSlice( GrainInfo *out_grainInfo ) override;

    ///
    /// dtor
    ///
    virtual ~PosixFlowWriter();

    /// \see FlowWriter
    virtual void flowRead() override;

private:
    /// The flow manager
    FlowManager::ptr _manager;
    /// The FlowData for the currently opened flow. null if no flow is opened.
    FlowData::ptr _flowData;
    /// The currently opened grain index. MXL_UNDEFINED_OFFSET if no grain is currently opened.
    uint64_t _currentIndex = MXL_UNDEFINED_OFFSET;
};

} // namespace mxl::lib