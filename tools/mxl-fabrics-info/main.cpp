// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ratio>
#include <string>
#include <system_error>
#include <vector>
#include <unistd.h>
#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <picojson/wrapper.h>
#include <mxl/fabrics.h>
#include <mxl/mxl.h>

namespace
{
    [[nodiscard]]
    constexpr char const* providerName(mxlFabricsProvider provider) noexcept
    {
        switch (provider)
        {
            case MXL_FABRICS_PROVIDER_ANY:   return "any";
            case MXL_FABRICS_PROVIDER_TCP:   return "tcp";
            case MXL_FABRICS_PROVIDER_VERBS: return "verbs";
            case MXL_FABRICS_PROVIDER_EFA:   return "efa";
            case MXL_FABRICS_PROVIDER_SHM:   return "shm";
        }
        return "unknown";
    }

    [[nodiscard]]
    constexpr std::optional<mxlFabricsProvider> providerFromName(std::string const& name) noexcept
    {
        if (name == "any")
        {
            return MXL_FABRICS_PROVIDER_ANY;
        }
        if (name == "tcp")
        {
            return MXL_FABRICS_PROVIDER_TCP;
        }
        if (name == "verbs")
        {
            return MXL_FABRICS_PROVIDER_VERBS;
        }
        if (name == "efa")
        {
            return MXL_FABRICS_PROVIDER_EFA;
        }
        if (name == "shm")
        {
            return MXL_FABRICS_PROVIDER_SHM;
        }
        return std::nullopt;
    }

    std::string capabilitiesString(std::uint64_t caps)
    {
        auto resultLength = std::size_t{0};
        auto capStrings = std::vector<std::string>{};
        if ((caps & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS) != 0)
        {
            resultLength += capStrings.emplace_back("BLOCKING_OPERATIONS").size();
        }
        if ((caps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) != 0)
        {
            resultLength += capStrings.emplace_back("REMOTE_WRITE").size();
        }
        if ((caps & MXL_FABRICS_IFACE_CAP_SEND_RECEIVE) != 0)
        {
            resultLength += capStrings.emplace_back("SEND_RECEIVE").size();
        }
        if (capStrings.empty())
        {
            resultLength += capStrings.emplace_back("<none>").size();
        }

        resultLength += (capStrings.size() - 1); // separating '|' chars

        auto result = capStrings.front();
        result.reserve(resultLength);
        for (auto it = capStrings.begin() + 1; it != capStrings.end(); ++it)
        {
            result.push_back('|');
            result.append(*it);
        }

        return result;
    }

    std::map<std::string, std::string> interfaceAttrs(char const* info)
    {
        if ((info == nullptr) || (::strlen(info) == 0))
        {
            return {};
        }
        auto v = picojson::value{};
        auto picoerr = picojson::parse(v, info);
        if (!picoerr.empty())
        {
            return {};
        }

        if (!v.is<picojson::object>())
        {
            return {};
        }

        auto out = std::map<std::string, std::string>{};
        for (auto const& [k, v] : v.get<picojson::object>())
        {
            if (k == "link_speed")
            {
                auto s = (v.get<double>() * std::giga::den) / std::giga::num;
                out[k] = fmt::format("{} gbit/s", s);
                continue;
            }

            out[k] = v.to_str();
        }

        return out;
    }

    void printInterface(mxlFabricsInterfaceConfig const& iface, std::size_t index, bool verbose)
    {
        auto const info = interfaceAttrs(iface.attr);
        auto const prov = providerName(iface.provider);
        if (verbose)
        {
            fmt::print("interface {}-{}\n", prov, index);
            fmt::print("  provider:      {}\n", prov);
            fmt::print("  node:          {}\n", (iface.address.node != nullptr) ? iface.address.node : "(none)");
            fmt::print("  max message:   {} bytes\n", iface.caps.maxMessageSize);
            fmt::print("  capabilities:  {}\n", capabilitiesString(iface.caps.flags));
            if (!info.empty())
            {
                fmt::print("  info:\n");
                for (auto const& [k, v] : info)
                {
                    fmt::print("    {}: {}\n", k, v);
                }
            }
        }
        else
        {
            auto deviceName = std::string{"<none>"};
            auto it = info.find("device_name");
            if (it != info.end())
            {
                deviceName = it->second;
            }
            fmt::print("interface {}-{} node {} dev {}\n", prov, index, iface.address.node, deviceName);
        }
    }

    int listInterfaces(mxlFabricsInstance instance, mxlFabricsInterfaceConfig const* query, bool verbose)
    {
        auto* list = static_cast<mxlFabricsInterfaceList*>(nullptr);
        if (auto const status = mxlFabricsGetInterfaces(instance, query, &list); status != MXL_STATUS_OK)
        {
            fmt::print(stderr, "Failed to query interfaces (mxlStatus = {}).\n", static_cast<int>(status));
            return 1;
        }

        auto providerCounts = std::map<std::string, std::size_t>{};
        for (auto* node = list; node != nullptr; node = node->next)
        {
            auto provider = providerName(node->interface.provider);
            auto count = providerCounts[provider]++;
            printInterface(node->interface, count, verbose);
        }

        mxlFabricsFreeInterfaceList(list);

        if (providerCounts.empty())
        {
            fmt::print(stderr, "No matching interfaces found.\n");
        }
        else if (verbose)
        {
            for (auto const& [provider, count] : providerCounts)
            {
                fmt::print(stderr, "Found [{:>3}] interfaces for [{:>5}] provider.\n", count, provider);
            }
        }

        return 0;
    }
}

int main(int argc, char** argv)
{
    auto app = CLI::App{"List the MXL fabrics interfaces available on this system."};

    auto domain = std::string{};
    auto providerArg = std::string{"any"};
    auto node = std::string{};
    auto verbose = false;
    app.add_option("-d,--domain", domain, "MXL domain directory. A temporary one is created and removed if omitted.");
    app.add_option("-p,--provider", providerArg, "Filter by provider: any, tcp, verbs, efa, shm.")->capture_default_str();
    app.add_option("-n,--node", node, "Filter by interface node (e.g. an IP address).");
    app.add_flag("-v,--verbose", verbose, "Make the output more verbose.");
    CLI11_PARSE(app, argc, argv);

    auto const provider = providerFromName(providerArg);
    if (!provider)
    {
        fmt::print(stderr, "Unknown provider '{}'. Expected one of: any, tcp, verbs, efa, shm.\n", providerArg);
        return 1;
    }

    // mxlFabricsGetInterfaces needs a valid mxlInstance, which in turn needs an existing domain
    // directory. The interface probe itself does not use the domain, so create a throwaway one
    // when the caller did not supply a domain.
    auto const useTempDomain = domain.empty();
    auto const domainPath =
        useTempDomain ? std::filesystem::temp_directory_path() / fmt::format("mxl-fabrics-info.{}", ::getpid()) : std::filesystem::path{domain};
    if (useTempDomain)
    {
        auto ec = std::error_code{};
        std::filesystem::create_directories(domainPath, ec);
        if (ec)
        {
            fmt::print(stderr, "Failed to create temporary domain '{}': {}\n", domainPath.string(), ec.message());
            return 1;
        }
    }

    auto rc = 0;
    if (auto* instance = mxlCreateInstance(domainPath.string().c_str(), ""))
    {
        // Only build a query when the caller asked to filter; otherwise list everything.
        auto query = mxlFabricsInterfaceConfig{};
        query.version = MXL_FABRICS_API_VERSION;
        query.provider = *provider;
        if (!node.empty())
        {
            query.address.node = node.c_str();
        }

        auto fabricsInstance = mxlFabricsInstance{nullptr};
        if (auto const s = mxlFabricsCreateInstance(instance, nullptr, &fabricsInstance); s != MXL_STATUS_OK)
        {
            fmt::print(stderr, "Failed to create fabrics instance (mxlStatus = {}).\n", static_cast<int>(s));
            mxlDestroyInstance(instance);
            return 1;
        }

        auto const filtering = (*provider != MXL_FABRICS_PROVIDER_ANY) || !node.empty();
        rc = listInterfaces(fabricsInstance, filtering ? &query : nullptr, verbose);

        mxlFabricsDestroyInstance(fabricsInstance);
        mxlDestroyInstance(instance);
    }
    else
    {
        fmt::print(stderr, "Failed to create an MXL instance for domain '{}'.\n", domainPath.string());
        rc = 1;
    }

    if (useTempDomain)
    {
        auto ec = std::error_code{};
        std::filesystem::remove_all(domainPath, ec);
    }

    return rc;
}
