// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Address.hpp
 * @brief Wrapper for libfabric endpoint addresses (fabric-level network identifiers)
 *
 * In libfabric/OFI, an "address" is an opaque binary identifier for a fabric endpoint.
 * It's analogous to a (IP, port) tuple in TCP, but:
 * - The format is provider-specific and opaque
 * - Can be much larger than an IPv4/IPv6 address
 * - Contains fabric-specific routing and connection information
 *
 * Addresses are used to:
 * - Insert remote endpoints into an AddressVector for connectionless communication
 * - Initiate connections to remote endpoints
 * - Identify which endpoint sent/received data
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>

namespace mxl::lib::fabrics::ofi
{
    /**
     * @class FabricAddress
     * @brief RAII wrapper around a libfabric endpoint address
     *
     * A FabricAddress represents the fabric-layer network identifier for an endpoint.
     * It encapsulates a variable-length binary blob whose format is determined by the
     * libfabric provider (TCP, EFA, Verbs, etc.).
     *
     * KEY OPERATIONS:
     * - Retrieve an address from an existing endpoint via fromFid()
     * - Serialize to/from base64 strings for out-of-band exchange (e.g., in TargetInfo)
     * - Compare addresses for equality
     * - Access the raw bytes for insertion into an AddressVector
     *
     * USAGE PATTERN:
     * 1. Target creates an endpoint and extracts its address: auto addr = FabricAddress::fromFid(ep)
     * 2. Target serializes to string: std::string encoded = addr.toBase64()
     * 3. String is sent to initiator (out-of-band communication)
     * 4. Initiator deserializes: auto remoteAddr = FabricAddress::fromBase64(encoded)
     * 5. Initiator inserts into AddressVector to enable communication
     */
    class FabricAddress final
    {
    public:
        /**
         * @brief Default constructor creates an empty (invalid) fabric address
         *
         * An empty address has zero size and should not be used for communication.
         * Use fromFid() or fromBase64() to create a valid address.
         */
        FabricAddress() = default;

        /**
         * @brief Factory method to retrieve the local fabric address of an endpoint
         *
         * @param fid Generic libfabric object handle (fid_t) - must be an endpoint or passive endpoint
         * @return FabricAddress containing the endpoint's local fabric address
         *
         * Internally calls fi_getname() to query the endpoint's address from libfabric.
         * The address format and length depend on the provider (e.g., TCP uses sockaddr, EFA uses custom format).
         *
         * @throws FabricException if fi_getname() fails
         */
        static FabricAddress fromFid(::fid_t fid);

        /**
         * @brief Serialize the address to a base64-encoded string
         *
         * @return std::string containing the base64-encoded address
         *
         * This is used to transmit addresses out-of-band (e.g., in TargetInfo objects sent from
         * target to initiator via configuration files, REST APIs, or signaling channels).
         *
         * Base64 encoding ensures the binary address data can be safely embedded in text formats
         * like JSON or XML.
         */
        [[nodiscard]]
        std::string toBase64() const;

        /**
         * @brief Deserialize a fabric address from a base64-encoded string
         *
         * @param data Base64-encoded string previously created by toBase64()
         * @return FabricAddress decoded from the string
         *
         * @throws std::runtime_error if the base64 decoding fails or the data is invalid
         *
         * This is the inverse of toBase64() - used by an initiator to reconstruct a target's
         * address from a serialized string received out-of-band.
         */
        static FabricAddress fromBase64(std::string_view data);

        /**
         * @brief Get mutable pointer to the raw address bytes
         *
         * @return void* pointing to the internal binary address buffer
         *
         * Used when passing the address to libfabric functions that require a mutable pointer,
         * such as fi_av_insert().
         */
        void* raw() noexcept;

        /**
         * @brief Get const pointer to the raw address bytes
         *
         * @return const void* pointing to the internal binary address buffer
         *
         * Used for read-only access to the address data.
         */
        [[nodiscard]]
        void const* raw() const noexcept;

        /**
         * @brief Get the size in bytes of the address
         *
         * @return std::size_t number of bytes in the address
         *
         * Address sizes vary by provider (e.g., TCP might be 16 bytes for sockaddr_in, EFA might be larger).
         */
        [[nodiscard]]
        std::size_t size() const noexcept;

        /**
         * @brief Compare two addresses for equality
         *
         * @param other Address to compare against
         * @return true if the addresses are byte-for-byte identical
         *
         * Two addresses are equal if they have the same length and contain the same bytes.
         * This implies they refer to the same fabric endpoint.
         */
        bool operator==(FabricAddress const& other) const noexcept;

    private:
        /**
         * @brief Private constructor from a byte vector
         *
         * @param addr Vector of bytes containing the address data
         *
         * Direct construction is private - use factory methods fromFid() or fromBase64() instead.
         */
        explicit FabricAddress(std::vector<std::uint8_t> addr);

        /**
         * @brief Internal helper to retrieve an endpoint's address via fi_getname()
         *
         * @param fid Libfabric object handle (endpoint or passive endpoint)
         * @return FabricAddress containing the retrieved address
         *
         * This is the implementation for fromFid(). It makes two calls to fi_getname():
         * 1. First with NULL buffer to query the address length
         * 2. Second with allocated buffer to retrieve the actual address bytes
         *
         * @throws FabricException on any libfabric API error
         */
        static FabricAddress retrieveFabricAddress(::fid_t);

    private:
        /**
         * @brief Internal storage for the address bytes
         *
         * The address is an opaque binary blob whose format and length are provider-specific.
         * Stored as a byte vector for flexibility and automatic memory management.
         */
        std::vector<std::uint8_t> _inner;
    };

}
