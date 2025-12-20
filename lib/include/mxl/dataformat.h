// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * Source and flow data formats as defined by AMWA NMOS IS-04, excluding `urn:x-nmos:format:data.event`.
     */
    typedef enum mxlDataFormat
    {
        MXL_DATA_FORMAT_UNSPECIFIED,
        MXL_DATA_FORMAT_VIDEO,
        MXL_DATA_FORMAT_AUDIO,
        MXL_DATA_FORMAT_DATA,
    } mxlDataFormat;

    /**
     * Return whether the specified format is valid.
     * \param[in] format the mxlDataFormat of interest.
     * \return 1 if the format specified in \p format is valid, otherwise 0.
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
     * Return whether the specified format is supported by MXL.
     * \param[in] format the mxlDataFormat of interest.
     * \return 1 if the format specified in \p format is supported, otherwise 0.
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
     * Return whether the specified format is operating in discrete grains.
     * \param[in] format the mxlDataFormat of interest.
     * \return 1 if the format specified in \p format is operating with
     *      continuous samples, otherwise 0.
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
     * Return whether the specified format is operating in continuous samples.
     * \param[in] format the mxlDataFormat of interest.
     * \return 1 if the format specified in \p format is operating with
     *      continuous samples, otherwise 0.
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
