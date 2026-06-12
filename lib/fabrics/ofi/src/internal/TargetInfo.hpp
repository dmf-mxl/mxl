// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include "mxl/fabrics.h"
#include "Address.hpp"
#include "Endpoint.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Information about the bounce buffer on the target, if the target uses one.
     */
    struct TargetInfoBounceBufferInfo
    {
        std::size_t entryCount; /**< Number of entries in the bounce buffer. */
        std::size_t entrySize;  /**< Size of each bounce buffer entry in bytes. */
    };

    /** \brief TargetInfo contains all the information required by an initiator to operate transfers to the given target.
     *
     * In the context of libfabric this means, the fi_addr, all the buffer addresses and sizes, and the remote protection key.
     */
    struct TargetInfo
    {
    public:
        /** \brief Cast an `mxlFabricsTargetInfo` opaque pointer to a TargetInfo pointer.
         */
        [[nodiscard]]
        static TargetInfo* fromAPI(mxlFabricsTargetInfo api) noexcept;

        /** \brief Cast a pointer to a `TargetInfo` instance to an `mxlFabricsTargetInfo` opaque pointer
         */
        [[nodiscard]]
        ::mxlFabricsTargetInfo toAPI() noexcept;

        /** \brief Serialize a TargetInfo instance to a JSON representation
         */
        [[nodiscard]]
        std::string toJSON() const;

        /** \brief Construct a TargetInfo instance from a JSON representation
         */
        [[nodiscard]]
        static TargetInfo fromJSON(std::string const& s);

        bool operator==(TargetInfo const& other) const noexcept;

    public:
        Endpoint::Id id;                /**< A unique identifier of the target's endpoint */
        RawFabricAddress fabricAddress; /**< Target's endpoint libfabric address */
        Provider provider;              /**< Remote targets provider */
        std::vector<RemoteRegion>
            remoteRegions; /**< Target's memory regions (and keys) which an initiator can operate on. This is used only for RMA operations */
        std::optional<TargetInfoBounceBufferInfo> bounceBufferInfo; /**< Information about the bounce buffer on the target, if the target uses one.*/
    };
}
