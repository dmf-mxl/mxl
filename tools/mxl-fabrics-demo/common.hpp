// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <csignal>
#include <cstdint>
#include <string>
#include <mxl/fabrics.h>
#include <mxl/mxl.h>

/// Set to 0 by SIGINT or SIGTERM. The main loops of the target and the initiator run while it is 1.
extern std::sig_atomic_t volatile g_running;

namespace mxl::fabrics::demo
{
    /// Command line options shared by the target and the initiator.
    struct Options
    {
        std::string domain;   ///< MXL domain directory (-d, --domain). Required.
        std::string flow;     ///< Target: path of a flow definition JSON file. Initiator: flow id (-f, --flow). Required.
        std::string info;     ///< Path of the file that holds the serialized target info (-t, --target-info). Required.
        std::string provider; ///< Provider name such as "tcp" or "verbs" (-p, --provider). Empty selects one automatically.
        std::string node;     ///< Local node address, usually an IP address (-n, --node). Empty selects one automatically.
        std::string service;  ///< Local service, usually a port number (-s, --service). Empty lets the provider choose.
    };

    /**
     * Parse the command line and install the SIGINT and SIGTERM handlers.
     * Prints the help text and exits the process for --help, an unknown option or a missing required option.
     * @param[in] argc Argument count passed to main().
     * @param[in] argv Argument vector passed to main().
     * @param[in] flowHelp Help text for the -f option, which differs between the target and the initiator.
     * @return The parsed options.
     */
    Options parseOptions(int argc, char** argv, char const* flowHelp);

    /**
     * Select a local fabrics interface.
     * Lists the interfaces that match the provider and node in @p options and picks the one with the highest provider priority
     * (EFA, then Verbs, then TCP, then SHM).
     * @param[in] instance A valid fabrics instance.
     * @param[in] options Provider, node and service requested by the user.
     * @param[out] out_list The interface list. The caller must free it with mxlFabricsFreeInterfaceList() after the target or
     * initiator setup call, because the strings in @p out_config point into it.
     * @param[out] out_config The selected interface, with the service taken from @p options.
     * @return MXL_STATUS_OK on success, MXL_ERR_NO_FABRIC if no interface matches, or the error returned by mxlFabricsGetInterfaces().
     */
    mxlStatus selectInterface(mxlFabricsInstance instance, Options const& options, mxlFabricsInterfaceList** out_list,
        mxlFabricsInterfaceConfig* out_config);

    /**
     * Read a whole file into a string.
     * @param[in] path Path of the file.
     * @param[out] out_content The file content.
     * @return true on success, false if the file could not be read.
     */
    bool readFile(char const* path, std::string* out_content);

    /**
     * Write a string to a file, replacing any previous content.
     * @param[in] path Path of the file.
     * @param[in] content The text to write.
     * @return true on success, false if the file could not be written.
     */
    bool writeFile(char const* path, std::string const& content);

    /**
     * Report a failed MXL call on stderr.
     * @param[in] status The status returned by the call.
     * @param[in] what Short description of the call, printed with the error.
     * @return true if @p status is MXL_STATUS_OK, false otherwise.
     */
    bool check(mxlStatus status, char const* what);

    /// Counts transfers and prints a summary about once per second.
    struct Counter
    {
        uint64_t transfers;   ///< Transfers since the last summary.
        uint64_t units;       ///< Slices or samples since the last summary.
        uint64_t lastPrintNs; ///< TAI time of the last summary in nanoseconds.
    };

    /**
     * Count one transfer and print a summary if a second has passed since the last one.
     * @param[in,out] counter The counter to update.
     * @param[in] units Number of slices or samples in this transfer.
     * @param[in] unitName Name of the unit for the summary, for example "slices".
     */
    void countTransfer(Counter* counter, uint64_t units, char const* unitName);
}
