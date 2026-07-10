// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <picojson/wrapper.h>
#include <mxl/fabrics.h>
#include "FabricInfo.hpp"
#include "Provider.hpp"

namespace mxl::lib::fabrics::ofi
{
    /**
     * Describes a discovered fabric interface. Created from a libfabric fi_info entry, translating
     * provider-level details into MXL-level capability flags, address information, and device attributes.
     * Instances are used to build the linked list returned by the public mxlFabricsGetInterfaces API.
     */
    class FabricInterfaceDescription
    {
    public:
        /**
         * Create a description from a libfabric fi_info entry. Translates libfabric capabilities
         * (FI_SEND, FI_WRITE, FI_RMA, etc.) into MXL capability flags and extracts NIC, domain,
         * and endpoint attributes. Returns nullopt if the provider is not recognized.
         */
        [[nodiscard]]
        static std::optional<FabricInterfaceDescription> create(FabricInfoView info);

        /**
         * Allocate a heap-allocated mxlFabricsInterfaceList node from this description.
         * String fields (node, service, attr) are strdup'd and owned by the node.
         * \param next optional pointer to the next node in the linked list.
         */
        [[nodiscard]]
        ::mxlFabricsInterfaceList* toRawLinkedListNode(::mxlFabricsInterfaceList* next = nullptr);

        /** Free a single linked list node and its strdup'd strings. Returns the next pointer. */
        [[nodiscard]]
        static ::mxlFabricsInterfaceList* freeRawLinkedListNode(::mxlFabricsInterfaceList*);

        /** \brief Return the raw MXL_FABRICS_IFACE_CAP_* flags for this interface. */
        [[nodiscard]]
        std::uint64_t caps() const noexcept
        {
            return _rawCaps;
        }

    private:
        explicit FabricInterfaceDescription(Provider, std::string, std::string, std::uint64_t, std::uint64_t, picojson::object);

    private:
        Provider _provider;
        std::string _node;
        std::string _service;
        std::uint64_t _maxMessageSize;
        std::uint64_t _rawCaps;
        picojson::object _attr;
    };
}
