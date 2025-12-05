// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
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
        struct ReadResult
        {
            std::optional<std::uint32_t> immData{std::nullopt}; /**< If a transfer was initiated with immediate data, this contains the data. */
        };

    public:
        virtual ~Target() = default;

        /** \brief Determine if new data can be consumed.
         *
         * A non-blocking operation that also drives the connection forward. Continuous invocation of this function is necessary for connection
         * establishment and ongoing progress.
         */
        virtual ReadResult read() = 0;

        /** \brief Determine if new data can be consumed.
         *
         * A blocking version of read. see read().
         */
        virtual ReadResult readBlocking(std::chrono::steady_clock::duration timeout) = 0;

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
        static TargetWrapper* fromAPI(mxlFabricsTarget api) noexcept;

        /** \brief Convert this TargetWrapper to its API representation.
         *
         * \return The mxlFabricsTarget representing this TargetWrapper.
         */
        mxlFabricsTarget toAPI() noexcept;

        /** \copydoc Target::read()
         */
        Target::ReadResult read();

        /** \copydoc Target::readBlocking(std::chrono::steady_clock::duration)
         */
        Target::ReadResult readBlocking(std::chrono::steady_clock::duration timeout);

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
