#include "FabricInterfaceList.hpp"
#include <ranges>
#include <utility>

namespace mxl::lib::fabrics::ofi
{
    FabricInterfaceList::FabricInterfaceList() = default;

    void FabricInterfaceList::push(FabricInterfaceDescription iface)
    {
        _interfaces.push_back(std::move(iface));
    }

    void FabricInterfaceList::clear()
    {
        _interfaces.clear();
    }

    ::mxlFabricsInterfaceList* FabricInterfaceList::toRawLinkedList() noexcept
    {
        auto previousNode = std::add_pointer_t<::mxlFabricsInterfaceList>{nullptr};
        for (auto& _interface : std::ranges::reverse_view(_interfaces))
        {
            previousNode = _interface.toRawLinkedListNode(previousNode);
        }

        return previousNode;
    }

    void FabricInterfaceList::freeRawLinkedList(::mxlFabricsInterfaceList* list) noexcept
    {
        while (list)
        {
            list = FabricInterfaceDescription::freeRawLinkedListNode(list);
        }
    }
}
