// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <array>
#include <variant>
#include <mxl/flowinfo.h>

namespace mxl::lib::fabrics::ofi
{

    /** \brief Describes the layout of data within memory regions.
     *
     */
    class DataLayout
    {
    public:
        /** \brief Video layout variant of DataLayout.
         */
        struct VideoDataLayout
        {
            std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> sliceSizes; /**< Number of slices per plane. \see MXL_MAX_PLANES_PER_GRAIN */
        };

        /** \brief Audio layout variant of DataLayout.
         */
        struct AudioDataLayout
        {
            std::uint32_t sampleSize;   /**< Size of each audio sample in bytes. */
            std::uint32_t channelCount; /**< Number of audio channels. */
            std::uint32_t bufferLength; /**< The number of samples per channel. */
        };

    public:
        /** \brief Create a DataLayout representing video data.
         * \param sliceSizes The slice sizes of each planes in the video data layout. \see MXL_MAX_PLANES_PER_GRAIN
         * \return A DataLayout representing the specified video layout.
         */
        [[nodiscard]]
        static DataLayout fromVideo(std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> const& sliceSizes) noexcept; // NOLINT

        /** \brief Create a DataLayout representing audio data.
         * \param sampleSize The size of each audio sample in bytes.
         * \param channelCount The number of audio channels.
         * \param bufferLength The number of samples per channel.
         * \return A DataLayout representing the specified audio layout.
         */
        [[nodiscard]]
        static DataLayout fromAudio(std::uint32_t sampleSize, std::uint32_t channelCount, std::uint32_t bufferLength) noexcept;

        /** \brief Check if the DataLayout is of video type.
         * \return true if the DataLayout is of video type, false otherwise.
         */
        [[nodiscard]]
        bool isVideo() const noexcept;

        /** \brief Check if the DataLayout is of audio type.
         * \return true if the DataLayout is of audio type, false otherwise.
         */
        [[nodiscard]]
        bool isAudio() const noexcept;

        /** \brief Get the DataLayout as a VideoDataLayout.
         * \throws std::bad_variant_access if the DataLayout is not of video type.
         */
        [[nodiscard]]
        VideoDataLayout const& asVideo() const noexcept;

        /** \brief Get the DataLayout as an AudioDataLayout.
         * \throws std::bad_variant_access if the DataLayout is not of audio type.
         */
        [[nodiscard]]
        AudioDataLayout const& asAudio() const noexcept;

    private:
        using InnerLayout = std::variant<VideoDataLayout, AudioDataLayout>;

    private:
        DataLayout(InnerLayout) noexcept;

    private:
        InnerLayout _inner;
    };

}
