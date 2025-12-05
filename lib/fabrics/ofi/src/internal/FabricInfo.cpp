// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FabricInfo.hpp"
#include <cstdint>
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include "Exception.hpp"
#include "FabricVersion.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Provider.hpp"

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

    FabricInfoList FabricInfoList::get(char const* node, char const* service, Provider provider, uint64_t caps, ::fi_ep_type epType)
    {
        ::fi_info* info;
        auto hints = FabricInfo::empty();

        /// These are the memory registration modes we currently support
        hints->domain_attr->mr_mode = FI_MR_LOCAL | FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY | FI_MR_HMEM;

        hints->mode = 0;
        hints->caps = caps;
        hints->ep_attr->type = epType;
        hints->fabric_attr->prov_name = strdup(fmt::to_string(provider).c_str());

        // hints: add condition to append FI_HMEM capability if needed!

        fiCall(::fi_getinfo, "Failed to get provider information", fiVersion(), node, service, FI_SOURCE, hints.raw(), &info);

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
