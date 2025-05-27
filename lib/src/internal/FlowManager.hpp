#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <uuid.h>
#include "DiscreteFlowData.hpp"

namespace mxl::lib
{
    ///
    /// Performs Flow CRUD operations as well as garbage collection of stale flows.
    ///
    /// CREATE
    /// Creates the flow resources in the base path (the MXL 'domain'). Flow resource contain
    ///  - <mxl_domain>/<flow_id>.mxl-flow          : The toplevel for all files belonging to MXLs representation of a the flow
    ///  - <mxl_domain>/<flow_id>.mxl-flow/.json    : A file containing the flow definition (NMOS Flow resource json)
    ///  - <mxl_domain>/<flow_id>.mxl-flow/.access  : A file used for access notifications by consumers of the flow
    ///  - <mxl_domain>/<flow_id>.mxl-flow/data     : The shared memory segment containing the `Flow`
    ///  - <mxl_domain>/<flow_id>.mxl-flow/grains/  : A directory containing the per grain shared memory segments.
    ///
    /// After creation, the FlowData associated with the new flow is stored in an internal cache.
    ///
    /// GET
    /// Return a cached FlowData for a flowId.  The flow must opened first.
    ///
    /// DELETE
    /// Delete a flow by flowId.  If the flow is opened it is first closed than all physical resources (see above) are deleted
    ///
    /// GARBAGE Collect
    /// POSIX IPC resources are not automatically reclaimed if processes using them die or misbehave. A shared memory
    /// file that has not been used for more than X seconds (by looking at the last read time or write time in the flow shared memory)
    ///
    /// LIST
    /// List all the flows found in the domain.
    ///
    class FlowManager
    {
    public:
        ///
        /// Creates a FlowManager.  Ideally there should only be on instance of the manager per instance of the SDK.
        ///
        /// \in_mxlDomain : The directory where the flows are stored.
        /// \throws std::filesystem::filesystem_error if the directory does not exist or is not accessible.
        ///
        FlowManager(std::filesystem::path const& in_mxlDomain);

        ///
        /// Destructor. Closes all opened resources
        ///
        ~FlowManager();

        ///
        /// Create a new Flow and associated grains. Opens it in read-write mode
        ///
        /// \param[in] flowId The id of the flow.
        /// \param[in] flowDef The json definition of the flow (NMOS Resource format). The flow is not parsed or validated. It is written as is.
        /// \param[in] flowFormat The flow data format.
        /// \param[in] grainCount How many individual grains to create.
        /// \param[in] grainRate The grain rate.
        /// \param[in] grainPayloadSize Size of the grain in host memory.  0 if the grain payload lives in device memory.
        ///
        std::unique_ptr<DiscreteFlowData> createFlow(uuids::uuid const& flowId, std::string const& flowDef, mxlDataFormat flowFormat, std::size_t grainCount, Rational const& grainRate,
            std::size_t grainPayloadSize);

        ///
        /// Open an existing flow by id.
        ///
        /// \param in_flowId The flow to open
        /// \param in_mode The flow access mode
        ///
        std::unique_ptr<DiscreteFlowData> openFlow(uuids::uuid const& in_flowId, AccessMode in_mode);

        ///
        /// Delete all resources associated to a flow
        /// \param flowData The flowdata resource if the flow was previously opened or created
        /// \return success or failure.
        ///
        bool deleteFlow(std::unique_ptr<FlowData>&& flowData);

        ///
        /// Delete all resources associated to a flow
        /// \param flowId The ID of the flow to delete.
        /// \return success or failure.
        ///
        bool deleteFlow(uuids::uuid const& flowId);

        ///
        /// Delete all flows that haven't been accessed for a period of time.
        ///
        void garbageCollect();

        ///
        /// \return List all flows on disk.
        ///
        std::vector<uuids::uuid> listFlows() const;

        ///
        /// Accessor for the mxl domain (base path where shared memory will be stored)
        /// \return The base path
        std::filesystem::path const& getDomain() const;

    private:
        std::filesystem::path _mxlDomain;
    };
} // namespace mxl::lib