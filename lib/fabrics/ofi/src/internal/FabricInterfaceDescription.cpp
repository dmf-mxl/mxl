// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#include "FabricInterfaceDescription.hpp"
#include <cstring> // IWYU pragma: keep; // ::strdup
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <unistd.h>
#include <rdma/fabric.h>
#include "CompletionQueue.hpp"
#include "FabricAddress.hpp"
#include "Format.hpp"
#include "Provider.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        [[nodiscard]]
        std::string getHostname()
        {
            char buf[256] = {};
            ::gethostname(buf, sizeof(buf));
            return buf;
        }

        void copyStringProperty(picojson::object& destination, char const* value, std::string const& name)
        {
            if ((value != nullptr) && (value[0] != '\0'))
            {
                destination[name] = picojson::value{std::string{value}};
            }
        }

        void copyStringProperty(picojson::object& destination, std::string const& value, std::string const& name)
        {
            if (!value.empty())
            {
                destination[name] = picojson::value{value};
            }
        }

        template<typename T>
        void copyNumberProperty(picojson::object& destination, T value, std::string const& name)
        {
            destination[name] = picojson::value{static_cast<double>(value)};
        }

        void copyNicProperties(picojson::object& destination, ::fid_nic* nic)
        {
            if (nic->device_attr)
            {
                auto const device = nic->device_attr;
                copyStringProperty(destination, device->name, "device_name");
                copyStringProperty(destination, device->device_id, "device_id");
                copyStringProperty(destination, device->vendor_id, "device_vendor_id");
                copyStringProperty(destination, device->device_version, "device_version");
                copyStringProperty(destination, device->driver, "device_driver");
                copyStringProperty(destination, device->firmware, "device_firmware");
            }
            if (nic->link_attr != nullptr)
            {
                auto const link = nic->link_attr;
                auto linkState = std::string{};
                switch (link->state)
                {
                    case FI_LINK_UP:   linkState = "up"; break;
                    case FI_LINK_DOWN: linkState = "down"; break;
                    default:           linkState = "unknown";
                }
                destination["link_state"] = picojson::value{linkState};
                if (link->speed != 0)
                {
                    destination["link_speed"] = picojson::value{static_cast<double>(link->speed)};
                }
                copyStringProperty(destination, link->address, "link_address");
                copyStringProperty(destination, link->network_type, "link_type");
            }
            if ((nic->bus_attr) && (nic->bus_attr->bus_type == FI_BUS_PCI))
            {
                auto const pci = nic->bus_attr->attr.pci;
                copyNumberProperty(destination, pci.domain_id, "pci_domain_id");
                copyNumberProperty(destination, pci.bus_id, "pci_bus_id");
                copyNumberProperty(destination, pci.device_id, "pci_device_id");
                copyNumberProperty(destination, pci.function_id, "pci_function_id");
            }
        }

        void copyDomainProperties(picojson::object& destination, ::fi_domain_attr* domain)
        {
            copyStringProperty(destination, domain->name, "fi_domain_name");
            copyNumberProperty(destination, domain->cq_cnt, "fi_domain_cq_cnt");
            copyNumberProperty(destination, domain->ep_cnt, "fi_domain_ep_cnt");
            copyNumberProperty(destination, domain->mr_cnt, "fi_domain_mr_cnt");
        }

    }

    FabricInterfaceDescription::FabricInterfaceDescription(Provider provider, std::string node, std::string service, std::uint64_t rawCaps,
        std::uint64_t maxMessageSize, picojson::object extraInfo)
        : _provider{provider}
        , _node{std::move(node)}
        , _service{std::move(service)}
        , _maxMessageSize{maxMessageSize}
        , _rawCaps{rawCaps}
        , _attr{std::move(extraInfo)}
    {}

    std::optional<FabricInterfaceDescription> FabricInterfaceDescription::create(FabricInfoView info)
    {
        auto caps = std::uint64_t{};
        auto attr = picojson::object{};
        auto const provider = providerFromString(info->fabric_attr->prov_name);
        if (!provider)
        {
            return std::nullopt;
        }

        auto faddr = FabricAddress::fromSource(info);
        auto node = std::string{};
        auto service = std::string{};
        if (*provider == Provider::SHM)
        {
            node = getHostname();
            service = faddr.service().value_or("");
        }
        else
        {
            node = faddr.node().value_or("");
        }

        if ((info->caps & (FI_SEND | FI_RECV)) != 0)
        {
            caps |= MXL_FABRICS_IFACE_CAP_SEND_RECEIVE;
        }
        if ((info->caps & (FI_REMOTE_WRITE | FI_WRITE | FI_RMA)) != 0)
        {
            caps |= MXL_FABRICS_IFACE_CAP_REMOTE_WRITE;
        }
        if ((*provider != Provider::EFA) || (CompletionQueue::isWaitObjectSupportedForEFA()))
        {
            caps |= MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS;
        }

        copyStringProperty(attr, fiToString(&info->addr_format, FI_TYPE_ADDR_FORMAT), "ep_addr_format");
        copyStringProperty(attr, fiToString(&info->ep_attr->protocol, FI_TYPE_PROTOCOL), "ep_protocol");
        copyStringProperty(attr, fiToString(&info->ep_attr->type, FI_TYPE_EP_TYPE), "ep_type");
        copyDomainProperties(attr, info->domain_attr);

        if (info->nic != nullptr)
        {
            copyNicProperties(attr, info->nic);
        }

        if (provider == Provider::TCP)
        {
            // For the TCP provider, the device_name is always the domain_attr->name.
            copyStringProperty(attr, info->domain_attr->name, "device_name");
        }

        return FabricInterfaceDescription{*provider, std::move(node), std::move(service), caps, info->ep_attr->max_msg_size, std::move(attr)};
    }

    ::mxlFabricsInterfaceList* FabricInterfaceDescription::toRawLinkedListNode(::mxlFabricsInterfaceList* next)
    {
        auto attr = std::add_pointer_t<char const>{nullptr};
        if (!_attr.empty())
        {
            attr = ::strdup(picojson::value{_attr}.serialize(false).c_str());
        }

        // clang-format off
        return new ::mxlFabricsInterfaceList{
            .next = next,
            .interface = {
                .version = MXL_FABRICS_API_VERSION,
                .provider = providerToAPI(_provider),
                .caps = {
                    .version = MXL_FABRICS_API_VERSION,
                    .flags = _rawCaps,
                    .maxMessageSize = _maxMessageSize,
                }, 
                .address = {
                    .node = ::strdup(_node.c_str()),
                    .service = ::strdup(_service.c_str()),
                }, 
                .attr = attr,
            },
        };
        // clang-format on
    }

    ::mxlFabricsInterfaceList* FabricInterfaceDescription::freeRawLinkedListNode(::mxlFabricsInterfaceList* list)
    {
        auto interface = list->interface;
        auto next = list->next;
        ::free(const_cast<char*>(interface.address.node));
        ::free(const_cast<char*>(interface.address.service));
        ::free(const_cast<char*>(interface.attr));
        delete list;
        return next;
    }
}
