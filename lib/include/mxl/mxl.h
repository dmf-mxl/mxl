// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#include <mxl/platform.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// MXL SDK Status codes.
    typedef enum mxlStatus
    {
        MXL_STATUS_OK = 0,
        MXL_ERR_UNKNOWN,
        MXL_ERR_FLOW_NOT_FOUND,
        MXL_ERR_OUT_OF_RANGE_TOO_LATE,
        MXL_ERR_OUT_OF_RANGE_TOO_EARLY,
        MXL_ERR_INVALID_FLOW_READER,
        MXL_ERR_INVALID_FLOW_WRITER,
        MXL_ERR_TIMEOUT,
        MXL_ERR_INVALID_ARG,
        MXL_ERR_CONFLICT,
        MXL_ERR_PERMISSION_DENIED,

        // A flow is invalid from a reader point of view if its data file has been replaced
        // (for example, if a writer restarted and recreated the flow)
        MXL_ERR_FLOW_INVALID,

        /* fabrics.h errors */
        MXL_ERR_STRLEN = 1024,
        MXL_ERR_INTERRUPTED,
        MXL_ERR_NO_FABRIC,
        MXL_ERR_INVALID_STATE,
        MXL_ERR_INTERNAL,
        MXL_ERR_NOT_READY,
        MXL_ERR_NOT_FOUND,
        MXL_ERR_EXISTS,
    } mxlStatus;

    /// MXL SDK Semantic versionning structure.
    typedef struct mxlVersionType
    {
        uint16_t major;
        uint16_t minor;
        uint16_t bugfix;
        uint16_t build;
    } mxlVersionType;

    ///
    /// Accessor for the version of the MXL SDK.
    /// \param out_version Pointer to a mxlVersionType structure to be filled with the version information.
    /// \return MXL_STATUS_OK if the version was successfully retrieved, MXL_ERR_INVALID_ARG if the pointer passed was NULL.
    ///
    MXL_EXPORT
    mxlStatus mxlGetVersion(mxlVersionType* out_version);

    /** An opaque type representing an MXL instance. */
    typedef struct mxlInstance_t* mxlInstance;

    ///
    /// Create a new MXL instance for a specific domain.
    ///
    /// \param in_mxlDomain The domain is the directory where the MXL ringbuffers files are stored.  It should live on a tmpfs filesystem.
    /// \param in_options Optional JSON string containing additional SDK options. Currently not used.
    /// \return A pointer to the MXL instance or NULL if the instance could not be created.
    ///
    MXL_EXPORT
    mxlInstance mxlCreateInstance(char const* in_mxlDomain, char const* in_options);

    ///
    /// Iterates over all flows in the MXL domain and deletes any flows that are
    /// no longer active (in other words, no readers or writers are using them. This typically
    /// happens when an application creating flows writers crashes or exits without cleaning up.
    /// A flow is considered active if a shared advisory lock is held on the data file of the flow.
    /// This function is automatically called when the instance is created but should be called periodically
    /// on a long running application to clean up any flows that are no longer active.
    ///
    MXL_EXPORT
    mxlStatus mxlGarbageCollectFlows(mxlInstance in_instance);

    ///
    /// Destroy the MXL instance.  This will also release all flows readers/writers associated with the instance.
    ///
    /// \param in_instance The MXL instance to destroy.
    /// \return MXL_STATUS_OK if the instance was successfully destroyed, MXL_ERR_INVALID_ARG if the pointer passed was NULL. MXL_ERR_UNKNOWN if an
    /// error occurred during destruction.
    ///
    MXL_EXPORT
    mxlStatus mxlDestroyInstance(mxlInstance in_instance);

#ifdef __cplusplus
}
#endif
