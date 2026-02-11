// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowManager.hpp
 * @brief CRUD operations for MXL flows (Create, Read, Update, Delete)
 *
 * FlowManager handles all filesystem operations for flows within an MXL domain.
 * It's the centralized authority for flow lifecycle management.
 *
 * RESPONSIBILITIES:
 * - CREATE: Allocate flow directory structure, shared memory files, grain files
 * - READ/OPEN: Map existing flow files into FlowData objects
 * - DELETE: Remove flow files and directories
 * - LIST: Enumerate all flows in the domain
 * - QUERY: Retrieve flow definitions (NMOS JSON)
 *
 * FILESYSTEM STRUCTURE CREATED:
 *   ${domain}/
 *     ${flowId}.mxl-flow/            -- Flow directory
 *       flow_def.json                -- NMOS flow definition (stored as-is)
 *       data                         -- Flow shared memory (FlowState + metadata)
 *       access                       -- Touch file for reader activity tracking
 *       grains/                      -- Grain directory (discrete flows only)
 *         data.0, data.1, ...        -- Individual grain files
 *       channels                     -- Sample ring buffer (continuous flows only)
 *
 * DESIGN:
 * - One FlowManager per Instance (owns the domain)
 * - No caching (FlowData objects owned by readers/writers, not manager)
 * - Atomic operations where possible (e.g., create with O_EXCL)
 * - Idempotent operations (e.g., delete non-existent flow succeeds)
 *
 * THREAD SAFETY:
 * - All operations are thread-safe (use filesystem atomicity)
 * - No internal locking (stateless except _mxlDomain path)
 * - Concurrent creates of same flow: one succeeds, others get "already exists"
 *
 * GARBAGE COLLECTION:
 * - deleteFlow() checks for advisory locks before deletion
 * - Flows with active readers/writers cannot be deleted (lock held)
 * - Stale flows (no locks) can be deleted safely
 */

#pragma once

#include <cstddef>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <uuid.h>
#include <mxl/platform.h>
#include "ContinuousFlowData.hpp"
#include "DiscreteFlowData.hpp"

namespace mxl::lib
{
    /**
     * Performs Flow CRUD operations (Create, Read, Update, Delete).
     *
     * One FlowManager per Instance, manages all flows in a single MXL domain.
     */
    class MXL_EXPORT FlowManager
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
        /// Create a new discrete flow together with its associated grains and open it in read-write mode.
        ///
        /// \param[in] flowId The id of the flow.
        /// \param[in] flowDef The json definition of the flow (NMOS Resource format). The flow is not parsed or validated. It is written as is.
        /// \param[in] flowFormat The flow data format. Must be one of the discrete formats.
        /// \param[in] grainCount How many individual grains to create.
        /// \param[in] grainRate The grain rate.
        /// \param[in] grainPayloadSize Size of the grain in host memory.  0 if the grain payload lives in device memory.
        /// \param[in] grainNumOfSlices Number of slices per grain.
        /// \param[in] grainSliceLengths Length of each slice in bytes.
        /// \param[in] maxSyncBatchSizeHintOpt Optional max sync batch size hint.
        /// \param[in] maxCommitBatchSizeHintOpt Optional max commit batch size hint
        /// \return (created, flowData) If the flow was created, the first returnd value is true. If the flow was opened instead, false will be
        /// returned. The second returned value is the flow data of the opened or created flow.
        ///
        std::pair<bool, std::unique_ptr<DiscreteFlowData>> createOrOpenDiscreteFlow(uuids::uuid const& flowId, std::string const& flowDef,
            mxlDataFormat flowFormat, std::size_t grainCount, mxlRational const& grainRate, std::size_t grainPayloadSize,
            std::size_t grainNumOfSlices, std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> grainSliceLengths,
            std::uint32_t maxSyncBatchSizeHintOpt = 1, std::uint32_t maxCommitBatchSizeHintOpt = 1);

        ///
        /// Create a new continuous flow together with its associated channel store and open it in read-write mode.
        ///
        /// \param[in] flowId The id of the flow.
        /// \param[in] flowDef The json definition of the flow (NMOS Resource format). The flow is not parsed or validated. It is written as is.
        /// \param[in] flowFormat The flow data format. Must be one of the continuous formats.
        /// \param[in] sampleRate The sample rate.
        /// \param[in] channelCount The number of channels in the flow.
        /// \param[in] sampleWordSize The size of one sample in bytes.
        /// \param[in] bufferLength The length of each channel buffer in samples.
        /// \param[in] maxSyncBatchSizeHintOpt Optional max sync batch size hint.
        /// \param[in] maxCommitBatchSizeHintOpt Optional max commit batch size hint
        /// \return (created, flowData) If the flow was created, the first returnd value is true. If the flow was opened instead, false will be
        /// returned. The second returned value is the flow data of the opened or created flow.
        ///
        std::pair<bool, std::unique_ptr<ContinuousFlowData>> createOrOpenContinuousFlow(uuids::uuid const& flowId, std::string const& flowDef,
            mxlDataFormat flowFormat, mxlRational const& sampleRate, std::size_t channelCount, std::size_t sampleWordSize, std::size_t bufferLength,
            std::uint32_t maxSyncBatchSizeHintOpt = 1, std::uint32_t maxCommitBatchSizeHintOpt = 1);

        /// Open an existing flow by id.
        ///
        /// \param[in] flowId The flow to open
        /// \param[in] mode The flow access mode
        ///
        std::unique_ptr<FlowData> openFlow(uuids::uuid const& flowId, AccessMode mode);

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
        /// \return List all flows on disk.
        ///
        std::vector<uuids::uuid> listFlows() const;

        ///
        /// \param flowId The ID of the flow to get the information about.
        /// \return The requested json flow definition.
        /// \throws std::filesystem::filesystem_error on flow not found
        /// \throws std::runtime_error on any other error
        ///
        std::string getFlowDef(uuids::uuid const& flowId) const;

        ///
        /// Accessor for the mxl domain (base path where shared memory will be stored)
        /// \return The base path
        std::filesystem::path const& getDomain() const;

    private:
        std::unique_ptr<DiscreteFlowData> openDiscreteFlow(std::filesystem::path const& flowDir, SharedMemoryInstance<Flow>&& sharedFlowInstance);
        std::unique_ptr<ContinuousFlowData> openContinuousFlow(std::filesystem::path const& flowDir, SharedMemoryInstance<Flow>&& sharedFlowInstance);

    private:
        std::filesystem::path _mxlDomain;
    };
} // namespace mxl::lib
