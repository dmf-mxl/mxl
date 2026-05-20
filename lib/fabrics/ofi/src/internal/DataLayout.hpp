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
        /** \brief Discrete layout variant of DataLayout.
         */
        struct Discrete
        {
            std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> sliceSizes; /**< Number of slices per plane. \see MXL_MAX_PLANES_PER_GRAIN */
        };

        /** \brief Continuous layout variant of DataLayout.
         */
        struct Continuous
        {
            std::size_t sampleSize;   /**< Size of each audio sample in bytes. */
            std::size_t channelCount; /**< Number of audio channels. */
            std::size_t bufferLength; /**< The number of samples per channel. */
        };

    public:
        /** \brief Create a DataLayout representing video data.
         * \param sliceSizes The slice sizes of each planes in the video data layout. \see MXL_MAX_PLANES_PER_GRAIN
         * \return A DataLayout representing the specified video layout.
         */
        [[nodiscard]]
        static DataLayout fromDiscrete(std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> const& sliceSizes) noexcept; // NOLINT

        /** \brief Create a DataLayout representing audio data.
         * \param sampleSize The size of each audio sample in bytes.
         * \param channelCount The number of audio channels.
         * \param bufferLength The number of samples per channel.
         * \return A DataLayout representing the specified audio layout.
         */
        [[nodiscard]]
        static DataLayout fromContinuous(std::size_t sampleSize, std::size_t channelCount, std::size_t bufferLength) noexcept;

        /** \brief Check if the DataLayout is of discrete type.
         * \return true if the DataLayout is of discrete type, false otherwise.
         */
        [[nodiscard]]
        bool isDiscrete() const noexcept;

        /** \brief Check if the DataLayout is of continuous type.
         * \return true if the DataLayout is of continuous type, false otherwise.
         */
        [[nodiscard]]
        bool isContinuous() const noexcept;

        /** \brief Get the DataLayout as Discrete.
         * \throws std::bad_variant_access if the DataLayout is not of discrete type.
         */
        [[nodiscard]]
        Discrete const& asDiscrete() const;

        /** \brief Get the DataLayout as Continuous.
         * \throws std::bad_variant_access if the DataLayout is not of continuous type.
         */
        [[nodiscard]]
        Continuous const& asContinuous() const;

    private:
        using InnerLayout = std::variant<Discrete, Continuous>;

    private:
        DataLayout(InnerLayout) noexcept;

    private:
        InnerLayout _inner;
    };

}
