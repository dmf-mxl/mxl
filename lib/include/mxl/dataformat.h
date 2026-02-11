// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file dataformat.h
 * @brief Media data-format enumeration and classification helpers.
 *
 * MXL handles two fundamentally different I/O models depending on the type
 * of media being exchanged:
 *
 *   **Discrete flows** (VIDEO, DATA)
 *     Each unit of media is a self-contained "grain" (a video frame, a chunk
 *     of ancillary data).  Grains are stored individually in the ring buffer
 *     and read/written one at a time via the Grain API.
 *
 *   **Continuous flows** (AUDIO)
 *     Media is a never-ending stream of samples organized in per-channel
 *     ring buffers.  Samples are read/written in arbitrary-sized windows
 *     via the Samples API.
 *
 * The helpers defined here let callers query whether a given format falls
 * into the discrete or continuous category so they can branch to the
 * appropriate API surface.
 *
 * The enum values align with the AMWA NMOS IS-04 format URNs:
 *   - MXL_DATA_FORMAT_VIDEO -> urn:x-nmos:format:video
 *   - MXL_DATA_FORMAT_AUDIO -> urn:x-nmos:format:audio
 *   - MXL_DATA_FORMAT_DATA  -> urn:x-nmos:format:data
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * Source and flow data formats as defined by AMWA NMOS IS-04,
     * excluding `urn:x-nmos:format:data.event`.
     *
     * These values are stored in the mxlCommonFlowConfigInfo.format field
     * of every flow's shared-memory header, so readers can discover what
     * kind of media a flow carries without parsing the JSON flow definition.
     */
    typedef enum mxlDataFormat
    {
        MXL_DATA_FORMAT_UNSPECIFIED, /**< Sentinel / default / not-yet-set.           */
        MXL_DATA_FORMAT_VIDEO,       /**< Video flow  (discrete grains, e.g. v210).   */
        MXL_DATA_FORMAT_AUDIO,       /**< Audio flow  (continuous samples, float32).   */
        MXL_DATA_FORMAT_DATA,        /**< Data flow   (discrete grains, e.g. ST 291). */
    } mxlDataFormat;

    /**
     * Check whether the given format value is a recognized MXL data format.
     *
     * "Valid" means it is one of VIDEO, AUDIO, or DATA.  UNSPECIFIED (0)
     * is intentionally excluded because it represents an uninitialised or
     * unknown state.
     *
     * @param[in] format  An integer that should be one of the mxlDataFormat values.
     * @return 1 if \p format is a recognized format, 0 otherwise.
     */
    inline int mxlIsValidDataFormat(int format)
    {
        switch (format)
        {
            case MXL_DATA_FORMAT_VIDEO:
            case MXL_DATA_FORMAT_AUDIO:
            case MXL_DATA_FORMAT_DATA:  return 1;

            default:                    return 0;
        }
    }

    /**
     * Check whether the given format is actively supported by this build of MXL.
     *
     * Today this returns the same result as mxlIsValidDataFormat(), but it
     * exists as a separate function so that future builds can selectively
     * disable individual formats (e.g., a lightweight audio-only build)
     * without changing the "valid" predicate.
     *
     * @param[in] format  An integer that should be one of the mxlDataFormat values.
     * @return 1 if \p format is supported in this build, 0 otherwise.
     */
    inline int mxlIsSupportedDataFormat(int format)
    {
        switch (format)
        {
            case MXL_DATA_FORMAT_VIDEO:
            case MXL_DATA_FORMAT_AUDIO:
            case MXL_DATA_FORMAT_DATA:  return 1;

            default:                    return 0;
        }
    }

    /**
     * Check whether the given format uses the **discrete grain** I/O model.
     *
     * Discrete formats store each unit of media (a video frame, a block of
     * ancillary data) as an individually addressable grain inside the ring
     * buffer.  Callers should use the Grain API (mxlFlowWriterOpenGrain,
     * mxlFlowReaderGetGrain, etc.) for these flows.
     *
     * Currently discrete formats: VIDEO, DATA.
     *
     * @param[in] format  An integer that should be one of the mxlDataFormat values.
     * @return 1 if \p format is a discrete-grain format, 0 otherwise.
     */
    inline int mxlIsDiscreteDataFormat(int format)
    {
        switch (format)
        {
            case MXL_DATA_FORMAT_VIDEO:
            case MXL_DATA_FORMAT_DATA:  return 1;

            default:                    return 0;
        }
    }

    /**
     * Check whether the given format uses the **continuous sample** I/O model.
     *
     * Continuous formats treat the ring buffer as a never-ending stream of
     * samples.  Each audio channel gets its own ring buffer, and callers
     * read/write arbitrary windows of samples via the Samples API
     * (mxlFlowWriterOpenSamples, mxlFlowReaderGetSamples, etc.).
     *
     * Currently continuous formats: AUDIO.
     *
     * @param[in] format  An integer that should be one of the mxlDataFormat values.
     * @return 1 if \p format is a continuous-sample format, 0 otherwise.
     */
    inline int mxlIsContinuousDataFormat(int format)
    {
        switch (format)
        {
            case MXL_DATA_FORMAT_AUDIO: return 1;

            default:                    return 0;
        }
    }
#ifdef __cplusplus
}
#endif
