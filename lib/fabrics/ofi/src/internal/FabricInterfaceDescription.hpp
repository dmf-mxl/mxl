#pragma once

#include <picojson/wrapper.h>
#include <mxl/fabrics.h>
#include "FabricInfo.hpp"
#include "Provider.hpp"

namespace mxl::lib::fabrics::ofi
{
    class FabricInterfaceDescription
    {
    public:
        static std::optional<FabricInterfaceDescription> create(FabricInfoView info);
        ::mxlFabricsInterfaceList* toRawLinkedListNode(::mxlFabricsInterfaceList* next = nullptr);
        static ::mxlFabricsInterfaceList* freeRawLinkedListNode(::mxlFabricsInterfaceList*);

        [[nodiscard]]
        std::uint64_t caps() const noexcept
        {
            return _rawCaps;
        }

    private:
        explicit FabricInterfaceDescription(Provider, std::string, std::string, std::uint64_t, std::uint64_t, picojson::object);

        Provider _provider;
        std::string _node;
        std::string _service;
        std::uint64_t _maxMessageSize;
        std::uint64_t _rawCaps;
        picojson::object _attr;
    };
}
