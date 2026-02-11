// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file AddressVector.hpp
 * @brief Address Vector (AV) wrapper - maps remote fabric addresses to local handles
 *
 * WHAT IS AN ADDRESS VECTOR?
 * In connectionless RDMA communication (e.g., with EFA provider), you don't establish
 * explicit connections to remote endpoints. Instead, you insert remote fabric addresses
 * into an Address Vector, which maps them to local integer handles (fi_addr_t).
 *
 * These handles are then used in RDMA operations:
 * - fi_write(endpoint, ..., destAddr, ...) where destAddr is an fi_addr_t from the AV
 * - The fabric uses the AV to look up routing information for that destination
 *
 * ANALOGY:
 * Think of the AV as a "phonebook" or "routing table":
 * - You insert a remote address (like a phone number) into the AV
 * - The AV gives you back a handle (like a speed-dial number)
 * - You use that handle in subsequent operations instead of the full address
 *
 * WHY USE AN ADDRESS VECTOR?
 * - Performance: Hardware can pre-resolve routing information
 * - Efficiency: Compact integer handles instead of variable-length addresses
 * - Required by connectionless providers (EFA, SHM in table mode)
 *
 * AV TYPES:
 * - FI_AV_TABLE (used here): Addresses are stored in a table, handles are integer indices
 * - FI_AV_MAP: Addresses can be hashed, handles may not be sequential
 *
 * USAGE PATTERN (for initiator sending to target):
 * 1. Target calls setup and shares its FabricAddress in TargetInfo
 * 2. Initiator receives the serialized TargetInfo
 * 3. Initiator deserializes the target's FabricAddress
 * 4. Initiator calls av.insert(targetAddr) to get an fi_addr_t handle
 * 5. Initiator uses that handle in endpoint.write(..., handle, ...)
 */

#pragma once

#include <memory>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include "Address.hpp"
#include "Domain.hpp"

namespace mxl::lib::fabrics::ofi
{

    /**
     * @class AddressVector
     * @brief RAII wrapper around a libfabric address vector (fi_av)
     *
     * Manages the lifecycle of an address vector and provides methods to insert/remove
     * remote addresses. The AV must be bound to an endpoint before the endpoint is enabled.
     */
    class AddressVector
    {
    public:
        /**
         * @struct Attributes
         * @brief Configuration attributes for creating an AddressVector
         *
         * These hints help the provider optimize resource allocation for the AV.
         */
        struct Attributes
        {
        public:
            /**
             * @brief Get default attributes suitable for typical use cases
             * @return Attributes with count=4 and epPerNode=0 (unknown)
             */
            static Attributes defaults() noexcept;

            /**
             * @brief Convert to the raw libfabric fi_av_attr structure
             * @return ::fi_av_attr Populated with this object's values plus FI_AV_TABLE type
             */
            [[nodiscard]]
            ::fi_av_attr toRaw() const noexcept;

        public:
            /**
             * @brief Expected number of addresses to be inserted into the AV
             *
             * The provider uses this hint to pre-allocate internal data structures.
             * Setting this accurately can improve performance and reduce memory overhead.
             */
            std::size_t count;

            /**
             * @brief Number of endpoints per remote node/host
             *
             * In distributed applications, multiple processes on each node may each create endpoints.
             * This hint tells the provider how many endpoints to expect per physical network address.
             * Example: 4 processes per node × 2 endpoints each = 8. If unknown, set to 0.
             */
            std::size_t epPerNode;
        };

        /**
         * @brief Factory method to create and open an AddressVector
         * @param domain The domain to create the AV in (AV is domain-scoped)
         * @param attr Configuration attributes (defaults are usually fine)
         * @return std::shared_ptr<AddressVector> Newly created AV
         * @throws FabricException if fi_av_open() fails
         */
        static std::shared_ptr<AddressVector> open(std::shared_ptr<Domain> domain, Attributes attr = Attributes::defaults());

        /**
         * @brief Destructor - closes the address vector and releases resources
         *
         * Calls fi_close() on the underlying fi_av. Any fi_addr_t handles obtained
         * from this AV become invalid after destruction.
         */
        ~AddressVector();

        // No copying - the AV owns a libfabric resource
        AddressVector(AddressVector const&) = delete;
        void operator=(AddressVector const&) = delete;

        /**
         * @brief Move constructor - transfers ownership of the AV
         * After the move, the source AddressVector's _raw pointer is nullptr.
         */
        AddressVector(AddressVector&&) noexcept;

        /**
         * @brief Move assignment - closes current AV and takes ownership of another
         */
        AddressVector& operator=(AddressVector&&);

        /**
         * @brief Insert a remote fabric address into the AV
         * @param addr The remote endpoint's fabric address to insert
         * @return ::fi_addr_t An integer handle for use in RDMA operations
         *
         * This maps the remote address to a local handle. Subsequent RDMA writes use
         * this handle instead of the full address.
         *
         * IMPORTANT: Caller is responsible for avoiding duplicate insertions.
         * Inserting the same address twice creates two separate handles pointing to
         * the same remote endpoint.
         *
         * @throws Exception if fi_av_insert() fails (e.g., AV is full, invalid address)
         */
        ::fi_addr_t insert(FabricAddress const& addr);

        /**
         * @brief Remove an address from the AV
         * @param addr The fi_addr_t handle (not FabricAddress!) to remove
         *
         * After removal, the handle should no longer be used in RDMA operations.
         * Typically called during shutdown when disconnecting from a target.
         * This function is noexcept - errors are logged but not thrown.
         */
        void remove(::fi_addr_t addr) noexcept;

        /**
         * @brief Convert a fabric address to a human-readable string
         * @param addr The fabric address to convert
         * @return std::string Provider-specific string representation
         *
         * Format depends on the provider:
         * - TCP: typically "IP:port"
         * - EFA: custom format with GID and QPN
         * - Verbs: IB GID and QPN
         * Useful for logging and debugging. Calls fi_av_straddr() internally.
         * @throws Exception if conversion fails
         */
        [[nodiscard]]
        std::string addrToString(FabricAddress const& addr) const;

        /**
         * @brief Get mutable access to the underlying fi_av pointer
         * @return ::fid_av* Raw libfabric address vector handle
         * Used when binding the AV to endpoints.
         */
        ::fid_av* raw() noexcept;

        /**
         * @brief Get const access to the underlying fi_av pointer
         * @return ::fid_av const* Raw libfabric address vector handle
         */
        [[nodiscard]]
        ::fid_av const* raw() const noexcept;

    private:
        /**
         * @brief Private constructor - use open() factory method instead
         * @param raw The raw fi_av pointer from fi_av_open()
         * @param domain The domain this AV belongs to (ownership retained)
         */
        AddressVector(fid_av* raw, std::shared_ptr<Domain> domain);

        /**
         * @brief Internal helper to close the AV and release resources
         *
         * Called by destructor and move assignment. Safe to call multiple times
         * (checks if _raw is non-null before closing).
         */
        void close();

    private:
        fid_av* _raw;                    ///< Raw libfabric AV handle (nullptr if moved-from)
        std::shared_ptr<Domain> _domain; ///< The domain this AV was created in (keeps domain alive)
    };
}
