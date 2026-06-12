#pragma once

#include <vector>
#include <mxl/fabrics.h>
#include "FabricInterfaceDescription.hpp"

namespace mxl::lib::fabrics::ofi
{
    class FabricInterfaceList
    {
    public:
        FabricInterfaceList();

        void push(FabricInterfaceDescription iface);
        void clear();

        ::mxlFabricsInterfaceList* toRawLinkedList() noexcept;
        static void freeRawLinkedList(::mxlFabricsInterfaceList*) noexcept;

    private:
        std::vector<FabricInterfaceDescription> _interfaces;
    };
}
