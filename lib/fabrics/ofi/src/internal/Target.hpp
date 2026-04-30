// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Abstract base class for Target implementations.
     */
    class Target
    {
    public:
        /** \brief Result of a read operation.
         */
        struct GrainReadResult
        {
            std::uint64_t grainIndex;
        };

        struct SampleReadResult
        {
            std::uint64_t headIndex;
            size_t count;
        };

        using ReadResult = std::variant<GrainReadResult, SampleReadResult>;

    public:
        virtual ~Target() = default;

        /** \brief Determine if new data can be consumed.
         *
         * A non-blocking operation that also drives the connection forward. Continuous invocation of this function is necessary for connection
         * establishment and ongoing progress.
         */
        virtual std::optional<GrainReadResult> readGrain() = 0;

        /** \brief Determine if new data can be consumed.
         *
         * A blocking version of read. see readGrain().
         */
        virtual std::optional<GrainReadResult> readGrainBlocking(std::chrono::steady_clock::duration timeout) = 0;

        /** \brief Determine if new data can be consumed.
         *
         * A non-blocking operation that also drives the connection forward. Continuous invocation of this function is necessary for connection
         * establishment and ongoing progress.
         */
        virtual std::optional<SampleReadResult> readSamples() = 0;

        /** \brief Determine if new data can be consumed.
         *
         * A blocking version of read. see readSamples().
         */
        virtual std::optional<SampleReadResult> readSamplesBlocking(std::chrono::steady_clock::duration timeout) = 0;

        /** \brief Shut down the target gracefully.
         * Initiates a graceful shutdown of the target and blocks until the shutdown is complete.
         * Can throw an exception if the shutdown is not successful. If this function throws the
         * target can now longer be used.
         */
        virtual void shutdown() = 0;

        /** \brief Represent an immediate data
         */
        struct ImmediateDataLocation
        {
        public:
            /** \brief Get the underlying local region of the immediate data.
             */
            [[nodiscard]]
            LocalRegion toLocalRegion() noexcept;

        public:
            std::uint64_t data; /**< The immediate data value. Libfabric uses a uint64_t, but some provider might only transfer 4 bytes. (Verbs) */
        };
    };

    /** \brief A wrapper around Target implementations.
     *
     * This wrapper creates an unspecified target that can be configured for
     * a specific type by calling the setup() method.
     */
    class TargetWrapper
    {
    public:
        /** \brief Convert an mxlFabricsTarget API object to its underlying TargetWrapper.
         *
         * \param api The mxlFabricsTarget to convert.
         * \return The TargetWrapper underlying the given mxlFabricsTarget.
         */
        [[nodiscard]]
        static TargetWrapper* fromAPI(mxlFabricsTarget api) noexcept;

        /** \brief Convert this TargetWrapper to its API representation.
         *
         * \return The mxlFabricsTarget representing this TargetWrapper.
         */
        [[nodiscard]]
        mxlFabricsTarget toAPI() noexcept;

        /** \copydoc Target::readGrain()
         */
        std::optional<Target::GrainReadResult> readGrain();

        /** \copydoc Target::readGrainBlocking(std::chrono::steady_clock::duration)
         */
        std::optional<Target::GrainReadResult> readGrainBlocking(std::chrono::steady_clock::duration timeout);

        /** \copydoc Target::readSamples()
         */
        std::optional<Target::SampleReadResult> readSamples();

        /** \copydoc Target::readSamplesBlocking(std::chrono::steady_clock::duration)
         */
        std::optional<Target::SampleReadResult> readSamplesBlocking(std::chrono::steady_clock::duration timeout);

        /** \brief Set up the target with the specified configuration.
         *
         * This method initializes the underlying target implementation
         * based on the provided configuration.
         *
         * \param config The configuration to use for setting up the target.
         */
        std::unique_ptr<TargetInfo> setup(mxlFabricsTargetConfig const& config);

    private:
        std::unique_ptr<Target> _inner; /**< The underlying target implementation. */
    };
}
