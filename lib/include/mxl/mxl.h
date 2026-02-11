// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file mxl.h
 * @brief Core MXL SDK entry point -- instance lifecycle, status codes, and versioning.
 *
 * This is the first header most consumers will include.  It defines:
 *
 *   1. **mxlStatus**      -- The universal error/success codes returned by every MXL function.
 *   2. **mxlVersionType** -- Semantic version of the SDK at runtime.
 *   3. **mxlInstance**    -- The opaque handle that binds all operations to a single MXL domain
 *                            (a tmpfs-backed directory of shared-memory ring buffers).
 *
 * Typical usage:
 * @code
 *     #include <mxl/mxl.h>
 *
 *     // Create an instance bound to the "/dev/shm/mxl" domain.
 *     mxlInstance inst = mxlCreateInstance("/dev/shm/mxl", NULL);
 *
 *     // ... create writers and readers via <mxl/flow.h> ...
 *
 *     mxlDestroyInstance(inst);   // Releases all readers/writers too.
 * @endcode
 */

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

    /* ======================================================================
     * Status codes
     * ======================================================================
     * Every MXL API function returns one of these values.  The first group
     * (values 0..N) covers the core Flow API; the second group (starting at
     * 1024) is reserved for the Fabrics networking layer.
     * ==================================================================== */

    /**
     * Universal return-code enum for the MXL SDK.
     *
     * Always check the return value of every MXL call -- any value other
     * than MXL_STATUS_OK indicates that the operation did not succeed and
     * that any [out] parameters should be considered uninitialised.
     */
    typedef enum mxlStatus
    {
        /* ---- Core Flow-API status codes ---- */

        MXL_STATUS_OK,                   /**< Success -- the operation completed normally.                                     */
        MXL_ERR_UNKNOWN,                 /**< An unexpected internal error occurred.                                           */
        MXL_ERR_FLOW_NOT_FOUND,          /**< The requested flow ID does not exist in the domain.                              */
        MXL_ERR_OUT_OF_RANGE_TOO_LATE,   /**< The requested grain/sample index has already been overwritten by the ring buffer. */
        MXL_ERR_OUT_OF_RANGE_TOO_EARLY,  /**< The requested grain/sample index has not been written yet.                       */
        MXL_ERR_INVALID_FLOW_READER,     /**< The supplied mxlFlowReader handle is NULL or refers to a released reader.        */
        MXL_ERR_INVALID_FLOW_WRITER,     /**< The supplied mxlFlowWriter handle is NULL or refers to a released writer.        */
        MXL_ERR_TIMEOUT,                 /**< A blocking wait exceeded the caller-supplied timeout.                            */
        MXL_ERR_INVALID_ARG,             /**< One or more arguments are NULL or otherwise invalid.                             */
        MXL_ERR_CONFLICT,                /**< The operation conflicts with the current state (e.g. duplicate writer).          */
        MXL_ERR_PERMISSION_DENIED,       /**< File-system permissions prevent the requested operation.                         */

        /**
         * The flow's backing data file has been replaced since this reader
         * was created (for example, because a writer process restarted and
         * re-created the flow).  The reader must be released and re-created.
         */
        MXL_ERR_FLOW_INVALID,

        /* ---- Fabrics networking layer status codes (see fabrics.h) ---- */

        MXL_ERR_STRLEN = 1024,   /**< A string argument exceeded the maximum allowed length.        */
        MXL_ERR_INTERRUPTED,     /**< The operation was interrupted (e.g. by a signal).             */
        MXL_ERR_NO_FABRIC,       /**< No suitable fabric provider was found on this system.        */
        MXL_ERR_INVALID_STATE,   /**< The fabric object is not in the correct state for this call. */
        MXL_ERR_INTERNAL,        /**< An internal error occurred inside the fabrics layer.         */
        MXL_ERR_NOT_READY,       /**< The fabric resource is not ready yet (still connecting).     */
        MXL_ERR_NOT_FOUND,       /**< The requested fabric resource (endpoint, region, ...) was not found. */
        MXL_ERR_EXISTS,          /**< The fabric resource already exists (duplicate registration). */
    } mxlStatus;

    /* ======================================================================
     * SDK version
     * ==================================================================== */

    /**
     * Semantic-versioning information for the MXL SDK.
     *
     * Retrieved at runtime via mxlGetVersion().  The `full` string is
     * owned by the library and must NOT be freed by the caller.
     */
    typedef struct mxlVersionType
    {
        uint16_t    major;  /**< Major version -- incremented on breaking API changes. */
        uint16_t    minor;  /**< Minor version -- incremented on backwards-compatible additions. */
        uint16_t    bugfix; /**< Patch version -- incremented on backwards-compatible bug fixes. */
        uint16_t    build;  /**< Build counter -- CI/CD build number or 0 for local builds. */
        char const* full;   /**< Human-readable version string, e.g. "1.0.0-alpha+42". Owned by the library. */
    } mxlVersionType;

    /**
     * Retrieve the version of the MXL SDK that is currently linked.
     *
     * @param[out] out_version  Pointer to a structure that will be filled
     *                          with the version information.  Must not be NULL.
     * @return MXL_STATUS_OK on success,
     *         MXL_ERR_INVALID_ARG if \p out_version is NULL.
     */
    MXL_EXPORT
    mxlStatus mxlGetVersion(mxlVersionType* out_version);

    /* ======================================================================
     * MXL Instance lifecycle
     * ======================================================================
     * An mxlInstance is the root object of the SDK.  It is bound to a
     * single MXL domain -- a directory (typically on tmpfs / /dev/shm)
     * that contains all shared-memory flow files.
     *
     * One process may create multiple instances pointing to different
     * domains, but each instance should only be created once per domain
     * within a process.
     * ==================================================================== */

    /** Opaque handle to an MXL instance.  Created by mxlCreateInstance(). */
    typedef struct mxlInstance_t* mxlInstance;

    /**
     * Create a new MXL instance bound to the specified domain directory.
     *
     * The domain directory is where all shared-memory flow files are stored
     * (ring-buffer data, grain payloads, flow definitions, etc.).  It
     * **must** reside on a tmpfs or memory-backed filesystem for
     * performance -- typically `/dev/shm/mxl` or a volume mounted as tmpfs.
     *
     * On creation the SDK automatically calls mxlGarbageCollectFlows() to
     * clean up any stale flows left behind by crashed processes.
     *
     * @param[in] in_mxlDomain  Absolute path to the domain directory.
     *                          Must not be NULL.
     * @param[in] in_options    Reserved for future use.  Pass NULL.
     * @return A valid mxlInstance on success, or NULL if creation failed
     *         (e.g. the directory does not exist or cannot be accessed).
     *
     * @see mxlDestroyInstance
     */
    MXL_EXPORT
    mxlInstance mxlCreateInstance(char const* in_mxlDomain, char const* in_options);

    /**
     * Scan the domain for stale flows and remove them.
     *
     * A flow is considered "stale" if no process holds a shared advisory
     * file lock on the flow's `data` file.  This typically happens when an
     * application crashes without properly releasing its writers.
     *
     * Called automatically during mxlCreateInstance(), but long-running
     * services should call this periodically (e.g. every few seconds) to
     * reclaim shared-memory resources that are no longer in use.
     *
     * @param[in] in_instance  A valid MXL instance.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlGarbageCollectFlows(mxlInstance in_instance);

    /**
     * Destroy the MXL instance and release **all** associated resources.
     *
     * This automatically releases every flow reader and writer that was
     * created through this instance.  After this call the handle is invalid
     * and must not be reused.
     *
     * @param[in] in_instance  The instance to destroy.  Must not be NULL.
     * @return MXL_STATUS_OK on success,
     *         MXL_ERR_INVALID_ARG if \p in_instance is NULL,
     *         MXL_ERR_UNKNOWN on internal cleanup failure.
     *
     * @see mxlCreateInstance
     */
    MXL_EXPORT
    mxlStatus mxlDestroyInstance(mxlInstance in_instance);

#ifdef __cplusplus
}
#endif
