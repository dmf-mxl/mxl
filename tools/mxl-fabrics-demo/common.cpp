// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "common.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <ada.h>
#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <mxl/time.h>

std::sig_atomic_t volatile g_running = 1;

namespace mxl::fabrics::demo
{
    namespace
    {
        /**
         * Signal handler for SIGINT and SIGTERM. Asks the main loop to stop.
         * @param[in] signal The signal number. Not used.
         */
        void onSignal(int /*signal*/)
        {
            g_running = 0;
        }

        /**
         * Rank a provider for automatic interface selection. Higher is preferred.
         * @param[in] provider The provider to rank.
         * @return The rank, 0 for unknown providers.
         */
        int providerPriority(mxlFabricsProvider provider)
        {
            switch (provider)
            {
                case MXL_FABRICS_PROVIDER_EFA:   return 4;
                case MXL_FABRICS_PROVIDER_VERBS: return 3;
                case MXL_FABRICS_PROVIDER_TCP:   return 2;
                case MXL_FABRICS_PROVIDER_SHM:   return 1;
                default:                         return 0;
            }
        }

        /**
         * Take the domain and the flow id from an MXL URI of the form mxl:///domain?id=<flow id>.
         * Remote authorities are not supported, and the URI must name exactly one flow.
         * @param[in] address The URI.
         * @param[in,out] options Receives the domain and the flow id, if the URI has them.
         * @return true on success, false if the URI is invalid. An error is printed on stderr.
         */
        bool applyUri(std::string const& address, Options* options)
        {
            auto const uri = ada::parse<ada::url_aggregator>(address);
            if (!uri || (uri->get_protocol() != "mxl:"))
            {
                fmt::print(stderr, "invalid MXL URI '{}'\n", address);
                return false;
            }
            if (!uri->get_hostname().empty() || !uri->get_port().empty())
            {
                fmt::print(stderr, "authority and port are not supported in the MXL URI\n");
                return false;
            }

            auto const path = uri->get_pathname();
            if (!path.empty())
            {
                options->domain = std::string{path};
            }

            auto const ids = ada::url_search_params{uri->get_search()}.get_all("id");
            if (ids.size() > 1)
            {
                fmt::print(stderr, "the MXL URI must name only one flow\n");
                return false;
            }
            if (!ids.empty())
            {
                options->flow = ids.front();
            }
            return true;
        }
    }

    Options parseOptions(int argc, char** argv, char const* flowHelp, bool acceptUri)
    {
        // std::signal() only fails for invalid signal numbers.
        static_cast<void>(std::signal(SIGINT, onSignal));
        static_cast<void>(std::signal(SIGTERM, onSignal));

        auto options = Options{};
        auto address = std::string{};
        auto app = CLI::App{"MXL fabrics demo"};
        auto* domainOpt = app.add_option("-d,--domain", options.domain, "MXL domain directory")->check(CLI::ExistingDirectory);
        auto* flowOpt = app.add_option("-f,--flow", options.flow, flowHelp);
        if (acceptUri)
        {
            // -d and -f become optional, because the URI can provide them. Their presence is checked after parsing.
            app.add_option("ADDRESS", address, "MXL URI of the local flow. Replaces -d and -f.");
            app.footer("MXL URI format:\n"
                       "    mxl:///domain?id=<flow id>\n"
                       "    See: https://github.com/dmf-mxl/mxl/docs/Addressability.md");
        }
        else
        {
            domainOpt->required();
            flowOpt->required();
        }
        app.add_option("-t,--target-info", options.info, "Target info file")->required();
        app.add_option("-p,--provider", options.provider, "Provider: tcp, verbs, efa or shm. Default: best available.");
        app.add_option("-n,--node", options.node, "Local node address. Default: first address of the provider.");
        app.add_option("-s,--service", options.service, "Local service, for example a port number");
        try
        {
            app.parse(argc, argv);
        }
        catch (CLI::ParseError const& e)
        {
            // Prints the help text or the error, and returns the exit status for it.
            std::exit(app.exit(e));
        }

        if (!address.empty() && !applyUri(address, &options))
        {
            std::exit(EXIT_FAILURE);
        }
        if (options.domain.empty() || options.flow.empty())
        {
            fmt::print(stderr, "the domain and the flow id must be given with -d and -f or with an MXL URI\n");
            std::exit(EXIT_FAILURE);
        }
        return options;
    }

    mxlStatus selectInterface(mxlFabricsInstance instance, Options const& options, mxlFabricsInterfaceList** out_list,
        mxlFabricsInterfaceConfig* out_config)
    {
        // The query only filters on provider and node. MXL_FABRICS_PROVIDER_ANY and a NULL node match everything.
        auto query = mxlFabricsInterfaceConfig{};
        query.version = MXL_FABRICS_API_VERSION;
        query.provider = MXL_FABRICS_PROVIDER_ANY;
        query.address.node = options.node.empty() ? nullptr : options.node.c_str();
        if (!options.provider.empty() && (mxlFabricsProviderFromString(options.provider.c_str(), &query.provider) != MXL_STATUS_OK))
        {
            fmt::print(stderr, "unknown provider '{}'\n", options.provider);
            return MXL_ERR_INVALID_ARG;
        }

        auto const status = mxlFabricsGetInterfaces(instance, &query, out_list);
        if (status != MXL_STATUS_OK)
        {
            return status;
        }

        mxlFabricsInterfaceConfig const* best = nullptr;
        for (auto const* entry = *out_list; entry != nullptr; entry = entry->next)
        {
            if ((best == nullptr) || (providerPriority(entry->interface.provider) > providerPriority(best->provider)))
            {
                best = &entry->interface;
            }
        }
        if (best == nullptr)
        {
            fmt::print(stderr, "no matching fabrics interface found\n");
            return MXL_ERR_NO_FABRIC;
        }

        *out_config = *best;
        out_config->address.service = options.service.empty() ? nullptr : options.service.c_str();

        char providerName[16] = {};
        auto providerNameSize = sizeof(providerName);
        static_cast<void>(mxlFabricsProviderToString(best->provider, providerName, &providerNameSize));
        fmt::print(stderr,
            "interface: provider {}, node {}, service {}, max message size {}\n",
            providerName,
            (best->address.node != nullptr) ? best->address.node : "(none)",
            options.service.empty() ? "(auto)" : options.service,
            best->caps.maxMessageSize);
        return MXL_STATUS_OK;
    }

    bool readFile(char const* path, std::string* out_content)
    {
        auto file = std::ifstream{path, std::ios::binary};
        if (!file)
        {
            return false;
        }
        out_content->assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
        return !file.bad();
    }

    bool writeFile(char const* path, std::string const& content)
    {
        auto file = std::ofstream{path, std::ios::binary | std::ios::trunc};
        file << content;
        return file.good();
    }

    bool check(mxlStatus status, char const* what)
    {
        if (status != MXL_STATUS_OK)
        {
            fmt::print(stderr, "{} failed with status {}\n", what, static_cast<int>(status));
            return false;
        }
        return true;
    }

    void countTransfer(Counter* counter, uint64_t units, char const* unitName)
    {
        counter->transfers += 1;
        counter->units += units;

        auto const now = mxlGetTime();
        if ((now - counter->lastPrintNs) >= 1'000'000'000ULL)
        {
            fmt::print(stderr, "{} transfers, {} {}\n", counter->transfers, counter->units, unitName);
            counter->transfers = 0;
            counter->units = 0;
            counter->lastPrintNs = now;
        }
    }
}
