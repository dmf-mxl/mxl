// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstring>

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
        MXL_DATA_FORMAT_MUX,
    } mxlDataFormat;

    typedef enum mxlVideoType
    {
        MXL_TYPE_V210 = 11,
        MXL_TYPE_PLANAR = 12,
        MXL_TYPE_UNKWOWN = -1
    } mxlVideoType;

    /**
     * Return valid video type iv kwnown or unknown (-1) if not
     * \param[in] type string value of type.
     * \return mxlVideoFormat of type
     */
    inline int  mxlVideoTypeFromString(const char *Type)
    {
        if( strcmp(Type, "video/v210") == 0 ) return MXL_TYPE_V210;
        if( strcmp(Type, "video/planar") == 0 ) return MXL_TYPE_PLANAR;

        // no match
        return MXL_TYPE_UNKWOWN;
    }
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
            case MXL_DATA_FORMAT_DATA:
            case MXL_DATA_FORMAT_MUX:   return 1;

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
