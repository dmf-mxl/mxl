#pragma once

#include <cstdint>
#include <array>
#include <variant>
#include "mxl/flowinfo.h"

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

    public:
        /** \brief Create a DataLayout representing video data.
         * \param sliceSizes The slice sizes of each planes in the video data layout. \see MXL_MAX_PLANES_PER_GRAIN
         * \return A DataLayout representing the specified video layout.
         */
        [[nodiscard]]
        static DataLayout fromVideo(std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> sliceSizes) noexcept; // NOLINT

        /** \brief Check if the DataLayout is of video type.
         * \return true if the DataLayout is of video type, false otherwise.
         */
        [[nodiscard]]
        bool isVideo() const noexcept;

        /** \brief Get the DataLayout as a VideoDataLayout.
         * \throws std::bad_variant_access if the DataLayout is not of video type.
         */
        [[nodiscard]]
        VideoDataLayout const& asVideo() const noexcept;

    private:
        using InnerLayout = std::variant<VideoDataLayout>;

    private:
        DataLayout(InnerLayout) noexcept;

    private:
        InnerLayout _inner;
    };

}
