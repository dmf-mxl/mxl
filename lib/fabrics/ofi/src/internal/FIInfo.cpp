#include "FIInfo.hpp"
#include <cstdint>
#include <cstring>
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include "Exception.hpp"
#include "FIVersion.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Provider.hpp"

namespace mxl::lib::fabrics::ofi
{
    // Main constructor, takes ownership of the provided fi_info
    FIInfo::FIInfo(::fi_info* raw) noexcept
        : _raw(raw)
    {}

    // Construct from a non-owning view of a fi_info
    FIInfo::FIInfo(FIInfoView view) noexcept
        : _raw(::fi_dupinfo(view.raw()))
    {}

    // Clone a raw fi_info and take ownership
    FIInfo FIInfo::clone(::fi_info const* info) noexcept
    {
        return FIInfo{::fi_dupinfo(info)};
    }

    // Own a raw fi_info
    FIInfo FIInfo::own(::fi_info* info) noexcept
    {
        return FIInfo{info};
    }

    // Allocate an empty fi_info
    FIInfo FIInfo::empty() noexcept
    {
        return FIInfo{::fi_allocinfo()};
    }

    FIInfo::~FIInfo() noexcept
    {
        free();
    }

    FIInfo::FIInfo(FIInfo const& other) noexcept
        : _raw(::fi_dupinfo(other._raw))
    {}

    void FIInfo::operator=(FIInfo const& other) noexcept
    {
        free();

        _raw = ::fi_dupinfo(other._raw);
    }

    FIInfo::FIInfo(FIInfo&& other) noexcept
        : _raw(other._raw)
    {
        other._raw = nullptr;
    }

    FIInfo& FIInfo::operator=(FIInfo&& other) noexcept
    {
        _raw = other._raw;
        other._raw = nullptr;

        return *this;
    }

    ::fi_info& FIInfo::operator*() noexcept
    {
        return *_raw;
    }

    ::fi_info const& FIInfo::operator*() const noexcept
    {
        return *_raw;
    }

    ::fi_info* FIInfo::operator->() noexcept
    {
        return _raw;
    }

    ::fi_info const* FIInfo::operator->() const noexcept
    {
        return _raw;
    }

    ::fi_info* FIInfo::raw() noexcept
    {
        return _raw;
    }

    ::fi_info const* FIInfo::raw() const noexcept
    {
        return _raw;
    }

    FIInfoView FIInfo::view() const noexcept
    {
        return FIInfoView{_raw};
    }

    void FIInfo::free() noexcept
    {
        if (_raw != nullptr)
        {
            ::fi_freeinfo(_raw);
            _raw = nullptr;
        }
    }

    FIInfoView::FIInfoView(::fi_info const* raw)
        : _raw(const_cast<::fi_info*>(raw))
    {}

    ::fi_info& FIInfoView::operator*() noexcept
    {
        return *_raw;
    }

    ::fi_info const& FIInfoView::operator*() const noexcept
    {
        return *_raw;
    }

    ::fi_info* FIInfoView::operator->() noexcept
    {
        return _raw;
    }

    ::fi_info const* FIInfoView::operator->() const noexcept
    {
        return _raw;
    }

    ::fi_info* FIInfoView::raw() noexcept
    {
        return _raw;
    }

    ::fi_info const* FIInfoView::raw() const noexcept
    {
        return _raw;
    }

    FIInfo FIInfoView::owned() noexcept
    {
        return FIInfo::clone(_raw);
    }

    FIInfoList FIInfoList::get(std::string node, std::string service, Provider provider, uint64_t caps)
    {
        ::fi_info* info;
        auto hints = FIInfo::empty();

        hints->domain_attr->mr_mode = FI_MR_LOCAL | FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY;

        hints->mode = 0;
        hints->caps = caps;
        hints->ep_attr->type = FI_EP_MSG;
        hints->fabric_attr->prov_name = strdup(fmt::to_string(provider).c_str());

        // hints: add condition to append FI_HMEM capability if needed!

        fiCall(::fi_getinfo, "Failed to get provider information", fiVersion(), node.c_str(), service.c_str(), 0, hints.raw(), &info);

        // fi_freeinfo(hints); // TODO: understand why this makes a crash.. according to the documentation hints shoud be freed after use.

        return FIInfoList{info};
    }

    FIInfoList FIInfoList::own(::fi_info* info) noexcept
    {
        return FIInfoList{info};
    }

    FIInfoList::FIInfoList(::fi_info* begin) noexcept
        : _begin(begin)
    {}

    FIInfoList::~FIInfoList()
    {
        free();
    }

    FIInfoList::FIInfoList(FIInfoList&& other) noexcept
        : _begin(other._begin)
    {
        other._begin = nullptr;
    }

    FIInfoList& FIInfoList::operator=(FIInfoList&& other) noexcept
    {
        free();

        _begin = other._begin;
        other._begin = nullptr;
        return *this;
    }

    FIInfoList::iterator FIInfoList::begin() noexcept
    {
        return iterator{_begin};
    }

    FIInfoList::iterator FIInfoList::end() noexcept
    {
        return iterator{nullptr};
    }

    FIInfoList::const_iterator FIInfoList::begin() const noexcept
    {
        return const_iterator{_begin};
    }

    FIInfoList::const_iterator FIInfoList::end() const noexcept
    {
        return const_iterator{nullptr};
    }

    FIInfoList::const_iterator FIInfoList::cbegin() const noexcept
    {
        return const_iterator{_begin};
    }

    FIInfoList::const_iterator FIInfoList::cend() const noexcept
    {
        return const_iterator{nullptr};
    }

    void FIInfoList::free()
    {
        if (_begin != nullptr)
        {
            ::fi_freeinfo(_begin);
            _begin = nullptr;
        }
    }
}
