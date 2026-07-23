// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FabricInfo.hpp"
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include "Exception.hpp"
#include "FabricAddress.hpp"
#include "FabricVersion.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Provider.hpp"
#include "ProviderConfig.hpp"

namespace mxl::lib::fabrics::ofi
{
    // Main constructor, takes ownership of the provided fi_info
    FabricInfo::FabricInfo(::fi_info* raw) noexcept
        : _raw(raw)
    {}

    // Construct from a non-owning view of a fi_info
    FabricInfo::FabricInfo(FabricInfoView view) noexcept
        : _raw(::fi_dupinfo(view.raw()))
    {}

    // Clone a raw fi_info and take ownership
    FabricInfo FabricInfo::clone(::fi_info const* info) noexcept
    {
        return FabricInfo{::fi_dupinfo(info)};
    }

    // Own a raw fi_info
    FabricInfo FabricInfo::own(::fi_info* info) noexcept
    {
        return FabricInfo{info};
    }

    // Allocate an empty fi_info
    FabricInfo FabricInfo::empty() noexcept
    {
        return FabricInfo{::fi_allocinfo()};
    }

    FabricInfo::~FabricInfo() noexcept
    {
        free();
    }

    FabricInfo::FabricInfo(FabricInfo const& other) noexcept
        : _raw(::fi_dupinfo(other._raw))
    {}

    void FabricInfo::operator=(FabricInfo const& other) noexcept
    {
        free();

        _raw = ::fi_dupinfo(other._raw);
    }

    FabricInfo::FabricInfo(FabricInfo&& other) noexcept
        : _raw(other._raw)
    {
        other._raw = nullptr;
    }

    FabricInfo& FabricInfo::operator=(FabricInfo&& other) noexcept
    {
        _raw = other._raw;
        other._raw = nullptr;

        return *this;
    }

    ::fi_info& FabricInfo::operator*() noexcept
    {
        return *_raw;
    }

    ::fi_info const& FabricInfo::operator*() const noexcept
    {
        return *_raw;
    }

    ::fi_info* FabricInfo::operator->() noexcept
    {
        return _raw;
    }

    ::fi_info const* FabricInfo::operator->() const noexcept
    {
        return _raw;
    }

    ::fi_info* FabricInfo::raw() noexcept
    {
        return _raw;
    }

    ::fi_info const* FabricInfo::raw() const noexcept
    {
        return _raw;
    }

    FabricInfoView FabricInfo::view() const noexcept
    {
        return FabricInfoView{_raw};
    }

    void FabricInfo::free() noexcept
    {
        if (_raw != nullptr)
        {
            ::fi_freeinfo(_raw);
            _raw = nullptr;
        }
    }

    FabricInfoView::FabricInfoView(::fi_info const* raw)
        : _raw(const_cast<::fi_info*>(raw))
    {}

    ::fi_info& FabricInfoView::operator*() noexcept
    {
        return *_raw;
    }

    ::fi_info const& FabricInfoView::operator*() const noexcept
    {
        return *_raw;
    }

    ::fi_info* FabricInfoView::operator->() noexcept
    {
        return _raw;
    }

    ::fi_info const* FabricInfoView::operator->() const noexcept
    {
        return _raw;
    }

    ::fi_info* FabricInfoView::raw() noexcept
    {
        return _raw;
    }

    ::fi_info const* FabricInfoView::raw() const noexcept
    {
        return _raw;
    }

    FabricInfo FabricInfoView::owned() noexcept
    {
        return FabricInfo::clone(_raw);
    }

    std::size_t FabricInfoView::txIovLimit() const noexcept
    {
        return _raw->tx_attr->iov_limit;
    }

    ::fi_ep_type FabricInfoView::endpointType() const noexcept
    {
        return _raw->ep_attr->type;
    }

    FabricInfoList FabricInfoList::get()
    {
        auto info = std::add_pointer_t<::fi_info>{nullptr};
        fiCall(::fi_getinfo, "Failed to get provider information", fiVersion(), nullptr, nullptr, 0, nullptr, &info);
        return FabricInfoList{info};
    }

    FabricInfoList FabricInfoList::getSourceInterfaces(ProviderConfig const& providerConfig, std::optional<FabricAddress> const& sourceAddress)
    {
        auto info = std::add_pointer_t<::fi_info>{nullptr};
        auto hints = FabricInfo::empty();

        auto node = std::string{};
        auto service = std::string{};
        auto nodeStringBuffer = std::add_pointer_t<char const>{nullptr};
        auto serviceStringBuffer = std::add_pointer_t<char const>{nullptr};

        // Provider name is owned by the fi_info object and must be ::strdupe'd here.
        hints->fabric_attr->prov_name = ::strdup(providerConfig.getProviderName().c_str());

        // Set hints defined in the provider config
        hints->domain_attr->mr_mode = providerConfig.getSupportedMemoryRegistrationModes();
        hints->ep_attr->type = providerConfig.getEndpointType();
        hints->caps = providerConfig.getCaps();

        if (sourceAddress)
        {
            // If a service name/port exists, create a std::string, and get the c_str()
            // the std::strings need to live through the call to fi_getinfo, so they are assigned to lvalues
            // in the parent scope too.
            if (auto nodeValue = sourceAddress->node(); nodeValue)
            {
                node = *nodeValue;
                nodeStringBuffer = node.c_str();
            }

            if (auto serviceValue = sourceAddress->service(); serviceValue)
            {
                service = *serviceValue;
                serviceStringBuffer = service.c_str();
            }

            // The verbs provider need the addr_format hint set for ipv6 to work correctly
            hints->addr_format = static_cast<std::uint32_t>(sourceAddress->addressFormat());
        }

        fiCall(::fi_getinfo,
            "Failed to get source interface information",
            fiVersion(),
            nodeStringBuffer,
            serviceStringBuffer,
            FI_SOURCE,
            hints.raw(),
            &info);
        return FabricInfoList{info};
    }

    FabricInfoList FabricInfoList::own(::fi_info* info) noexcept
    {
        return FabricInfoList{info};
    }

    FabricInfoList::FabricInfoList(::fi_info* begin) noexcept
        : _begin(begin)
    {}

    FabricInfoList::~FabricInfoList()
    {
        free();
    }

    FabricInfoList::FabricInfoList(FabricInfoList&& other) noexcept
        : _begin(other._begin)
    {
        other._begin = nullptr;
    }

    FabricInfoList& FabricInfoList::operator=(FabricInfoList&& other) noexcept
    {
        free();

        _begin = other._begin;
        other._begin = nullptr;
        return *this;
    }

    FabricInfoList::iterator FabricInfoList::begin() noexcept
    {
        return iterator{_begin};
    }

    FabricInfoList::iterator FabricInfoList::end() noexcept
    {
        return iterator{nullptr};
    }

    FabricInfoList::const_iterator FabricInfoList::begin() const noexcept
    {
        return const_iterator{_begin};
    }

    FabricInfoList::const_iterator FabricInfoList::end() const noexcept
    {
        return const_iterator{nullptr};
    }

    FabricInfoList::const_iterator FabricInfoList::cbegin() const noexcept
    {
        return const_iterator{_begin};
    }

    FabricInfoList::const_iterator FabricInfoList::cend() const noexcept
    {
        return const_iterator{nullptr};
    }

    void FabricInfoList::free()
    {
        if (_begin != nullptr)
        {
            ::fi_freeinfo(_begin);
            _begin = nullptr;
        }
    }
}
