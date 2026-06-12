#pragma once

#include <vector>
#include <mxl/fabrics.h>
#include "FabricInterfaceDescription.hpp"

namespace mxl::lib::fabrics::ofi
{
    /**
     * Collects FabricInterfaceDescription entries discovered during interface probing and converts
     * them to the heap-allocated mxlFabricsInterfaceList linked list returned by the public C API.
     */
    class FabricInterfaceList
    {
    public:
        FabricInterfaceList();

        /** Append an interface description to the list. */
        void push(FabricInterfaceDescription iface);

        /** Remove all collected interface descriptions. */
        void clear();

        /**
         * Convert the collected descriptions into a heap-allocated mxlFabricsInterfaceList linked list.
         * Ownership of the returned list transfers to the caller; free with freeRawLinkedList().
         */
        ::mxlFabricsInterfaceList* toRawLinkedList() noexcept;

        /** Free an entire linked list previously created by toRawLinkedList(). */
        static void freeRawLinkedList(::mxlFabricsInterfaceList*) noexcept;

    private:
        std::vector<FabricInterfaceDescription> _interfaces;
    };
}
