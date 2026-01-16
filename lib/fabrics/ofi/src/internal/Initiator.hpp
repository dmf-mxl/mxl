// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include "Endpoint.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Abstract base class for Initiator implementations.
     */
    class Initiator
    {
    public:
        virtual ~Initiator() = default;

        /** \brief Add a target to the initiator.
         *
         * The is a non-blocking operation. The local endpoint for this target will only actively start connecting to its remote counterpart when
         * control is passed to makeProgress() or makeProgressBlocking(). The local endpoint for this target will only accept write requests after the
         * progress functions return false.
         */
        virtual void addTarget(TargetInfo const& targetInfo) = 0;

        /** \brief Remove a target from the initiator.
         *
         * This is a non-blocking operation. The target is only removed after makeProgress() or makeProgressBlocking() returns false.
         */
        virtual void removeTarget(TargetInfo const& targetInfo) = 0;

        /** \brief Transfer a grain to all targets.
         *
         * This is a non-blocking operation. The transfer is complete only after makeProgress() or makeProgressBlocking() returns false.
         *
         * \param grainIndex The index of the grain to transfer.
         * \param startSlice The start slice in the slice range to transfer. This is inclusive.
         * \param endSlice The end slice in the slice range to transfer. This is exclusive.
         */
        virtual void transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice) = 0;

        /** \brief Transfer a grain to a specific target.
         *
         * This is a non-blocking operation. The transfer is complete only after makeProgress() or makeProgressBlocking() returns false.
         *
         * \param targetId The ID of the target to transfer the grain to.
         * \param localIndex The index of the local grain to transfer from.
         * \param remoteIndex The index of the remote grain to transfer to.
         * \param payloadOffset The payload offset within the grain.
         * \param startSlice The start slice in the slice range to transfer. This is inclusive.
         * \param endSlice The end slice in the slice range to transfer. This is exclusive.
         */
        virtual void transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t payloadOffset,
            std::uint16_t startSlice, std::uint16_t endSlice) = 0;

        /** \brief Attempts to progress execution, including connection management and data operations.
         *
         * This is the non-blocking version of the progress function.
         */
        virtual bool makeProgress() = 0;

        /** \brief Attempts to progress execution, including connection management and data operations.
         *
         * This is the blocking version of the progress function.
         */
        virtual bool makeProgressBlocking(std::chrono::steady_clock::duration) = 0;

        /** \brief Shut down the initiator gracefully.
         *
         * Initiates a graceful shutdown of the initiator and blocks until the shutdown is complete.
         * Can throw an exception if the shutdown is not successful. If this function throws the
         * initiator can no longer be used.
         */
        virtual void shutdown() = 0;
    };

    /** \brief A wrapper around Initiator implementations.
     *
     * This wrapper creates an unspecified initiator that can be configured for
     * a specific type by calling the setup() method.
     */
    class InitiatorWrapper
    {
    public:
        /** \brief Convert an mxlFabricsInitiator API object to its underlying InitiatorWrapper.
         *
         * \param api The mxlFabricsInitiator to convert.
         * \return The InitiatorWrapper underlying the given mxlFabricsInitiator.
         */
        [[nodiscard]]
        static InitiatorWrapper* fromAPI(mxlFabricsInitiator api) noexcept;

        /** \brief Convert this InitiatorWrapper to its API representation.
         *
         * \return The mxlFabricsInitiator representing this InitiatorWrapper.
         */
        [[nodiscard]]
        mxlFabricsInitiator toAPI() noexcept;

        /** \brief Set up the initiator with the specified configuration.
         *
         * This method initializes the underlying initiator implementation
         * based on the provided configuration.
         *
         * \param config The configuration to use for setting up the initiator.
         */
        void setup(mxlFabricsInitiatorConfig const& config);

        /** \copydoc Initiator::addTarget()
         */
        void addTarget(TargetInfo const& targetInfo);

        /** \copydoc Initiator::removeTarget()
         */
        void removeTarget(TargetInfo const& targetInfo);

        /** \copydoc Initiator::transferGrain()
         */
        void transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice);

        /** \copydoc Initiator::transferGrainToTarget()
         */
        void transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t payloadOffset,
            std::uint16_t startSlice, std::uint16_t endSlice);

        /** \copydoc Initiator::makeProgress()
         */
        bool makeProgress();

        /** \copydoc Initiator::makeProgressBlocking()
         */
        bool makeProgressBlocking(std::chrono::steady_clock::duration);

    private:
        std::unique_ptr<Initiator> _inner; /**< The underlying initiator implementation. */
    };
}
