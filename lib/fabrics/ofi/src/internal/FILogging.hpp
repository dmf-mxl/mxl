#pragma once

namespace mxl::lib::fabrics::ofi
{
    /// Initalize logging for libfabric. After this call, all libfabric internal logs
    /// will be written to the mxl logging system. It is save to call this function multiple times
    /// and concurrently from different threads.
    /// If this function throws an exception, the program cannot continue.
    void fiInitLogging();
}
