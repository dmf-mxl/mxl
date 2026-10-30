// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file cuda_sink.cpp
 *
 * Display a v210 flow whose payload is in CUDA device memory.
 * mxlFlowReaderGetGrainSlot returns the slot. The picture is copied with cudaMemcpy into a GStreamer
 * buffer and pushed to autovideosink. An optional device UUID asks the cuda-linear backend to map the
 * payload on another GPU of the same machine.
 */

#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cuda_runtime_api.h>
#include <fmt/format.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <CLI/CLI.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "utils.hpp"

namespace
{
    auto volatile g_exit_requested = std::sig_atomic_t{0};

    void onSignal(int) noexcept
    {
        g_exit_requested = 1;
    }

    [[noreturn]]
    void fail(std::string const& what)
    {
        throw std::runtime_error{what};
    }

    void requireMxl(mxlStatus status, std::string const& what)
    {
        if (status != MXL_STATUS_OK)
        {
            fail(fmt::format("{} failed with status {}", what, static_cast<int>(status)));
        }
    }

    void requireCuda(cudaError_t error, std::string const& what)
    {
        if (error != cudaSuccess)
        {
            fail(fmt::format("{} failed: {}", what, ::cudaGetErrorString(error)));
        }
    }

    std::string payloadOptions(std::string const& deviceUuid)
    {
        if (deviceUuid.empty())
        {
            return {};
        }
        if (deviceUuid.find_first_of("\\\"") != std::string::npos)
        {
            fail("device UUID contains a character that cannot be placed in the reader options");
        }
        return fmt::format(R"({{"payload":{{"deviceUuid":"{}"}}}})", deviceUuid);
    }

    std::string flowDefinition(mxlInstance instance, std::string const& flowId)
    {
        auto buffer = std::vector<char>(4096);
        auto bufferSize = buffer.size();
        auto status = ::mxlGetFlowDef(instance, flowId.c_str(), buffer.data(), &bufferSize);
        if ((status == MXL_ERR_INVALID_ARG) && (bufferSize > buffer.size()))
        {
            buffer.resize(bufferSize);
            status = ::mxlGetFlowDef(instance, flowId.c_str(), buffer.data(), &bufferSize);
        }
        requireMxl(status, "mxlGetFlowDef");
        if (bufferSize == 0)
        {
            fail("The flow definition is empty.");
        }
        return std::string{buffer.data(), bufferSize - 1U};
    }

    class Display
    {
    public:
        Display(std::uint64_t width, std::uint64_t height, mxlRational rate)
        {
            auto const description = fmt::format(
                "appsrc name=appsource is-live=true format=time ! "
                "video/x-raw,format=v210,width={},height={},framerate={}/{} ! "
                "videoconvert ! autovideosink",
                width,
                height,
                rate.numerator,
                rate.denominator);
            MXL_INFO("GStreamer pipeline: {}", description);

            auto* error = static_cast<GError*>(nullptr);
            _pipeline = ::gst_parse_launch(description.c_str(), &error);
            if ((_pipeline == nullptr) || (error != nullptr))
            {
                auto const message = (error != nullptr) ? std::string{error->message} : std::string{"parse failed"};
                if (error != nullptr)
                {
                    ::g_error_free(error);
                }
                fail("Failed to create the GStreamer pipeline: " + message);
            }

            _appSource = ::gst_bin_get_by_name(GST_BIN(_pipeline), "appsource");
            if (_appSource == nullptr)
            {
                fail("The pipeline has no appsrc.");
            }
            auto* caps = ::gst_caps_new_simple("video/x-raw",
                "format",
                G_TYPE_STRING,
                "v210",
                "width",
                G_TYPE_INT,
                static_cast<int>(width),
                "height",
                G_TYPE_INT,
                static_cast<int>(height),
                "framerate",
                GST_TYPE_FRACTION,
                static_cast<int>(rate.numerator),
                static_cast<int>(rate.denominator),
                nullptr);
            ::g_object_set(G_OBJECT(_appSource), "caps", caps, "block", TRUE, nullptr);
            ::gst_caps_unref(caps);
        }

        Display(Display const&) = delete;
        Display& operator=(Display const&) = delete;

        ~Display()
        {
            if (_appSource != nullptr)
            {
                ::gst_object_unref(_appSource);
            }
            if (_pipeline != nullptr)
            {
                ::gst_element_set_state(_pipeline, GST_STATE_NULL);
                ::gst_object_unref(_pipeline);
            }
        }

        void start()
        {
            if (::gst_element_set_state(_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            {
                fail("Failed to start the GStreamer pipeline. A display is required for autovideosink.");
            }
        }

        void push(void const* devicePointer, std::size_t size, std::uint64_t pts)
        {
            throwIfBusError();
            auto* buffer = ::gst_buffer_new_allocate(nullptr, size, nullptr);
            if (buffer == nullptr)
            {
                fail("Failed to allocate a GStreamer buffer.");
            }
            auto map = GstMapInfo{};
            if (!::gst_buffer_map(buffer, &map, GST_MAP_WRITE))
            {
                ::gst_buffer_unref(buffer);
                fail("Failed to map a GStreamer buffer.");
            }
            requireCuda(::cudaMemcpy(map.data, devicePointer, size, cudaMemcpyDeviceToHost), "cudaMemcpy");
            ::gst_buffer_unmap(buffer, &map);
            GST_BUFFER_PTS(buffer) = pts;
            auto const result = ::gst_app_src_push_buffer(GST_APP_SRC(_appSource), buffer);
            if (result != GST_FLOW_OK)
            {
                fail(fmt::format("appsrc rejected a buffer with flow status {}", static_cast<int>(result)));
            }
        }

    private:
        void throwIfBusError()
        {
            auto* bus = ::gst_element_get_bus(_pipeline);
            auto* message = ::gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            ::gst_object_unref(bus);
            if (message == nullptr)
            {
                return;
            }
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
            {
                auto* error = static_cast<GError*>(nullptr);
                ::gst_message_parse_error(message, &error, nullptr);
                auto const text = (error != nullptr) ? std::string{error->message} : std::string{"unknown GStreamer error"};
                if (error != nullptr)
                {
                    ::g_error_free(error);
                }
                ::gst_message_unref(message);
                fail(text);
            }
            ::gst_message_unref(message);
            fail("The display pipeline reached end of stream.");
        }

        GstElement* _pipeline{nullptr};
        GstElement* _appSource{nullptr};
    };

    class CudaReader
    {
    public:
        CudaReader(std::string const& domain, std::string const& flowId, std::string const& options)
        {
            _instance = ::mxlCreateInstance(domain.c_str(), "");
            if (_instance == nullptr)
            {
                fail("Failed to create an MXL instance.");
            }
            _definition = flowDefinition(_instance, flowId);
            auto const optionsText = options.empty() ? "" : options.c_str();
            requireMxl(::mxlCreateFlowReader(_instance, flowId.c_str(), optionsText, &_reader), "mxlCreateFlowReader");
            requireMxl(::mxlFlowReaderGetConfigInfo(_reader, &_config), "mxlFlowReaderGetConfigInfo");
            if (_config.common.format != MXL_DATA_FORMAT_VIDEO)
            {
                fail("mxl-gst-cuda-sink displays video flows only.");
            }

            auto layout = mxlGrainStorageLayout{};
            requireMxl(::mxlFlowReaderGetStorageLayout(_reader, &layout), "mxlFlowReaderGetStorageLayout");
            if (layout.storageType != MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER)
            {
                fail("The flow payload is not in CUDA device memory.");
            }
            _slots.resize(layout.slotCount);
            requireMxl(::mxlFlowReaderMapSlots(_reader, layout.slotCount, _slots.data()), "mxlFlowReaderMapSlots");
            requireMxl(::mxlFlowReaderGetStorageLayout(_reader, &layout), "mxlFlowReaderGetStorageLayout");
            requireCuda(::cudaSetDevice(layout.deviceIndex), "cudaSetDevice");
            MXL_INFO("Reading payload on CUDA device {}", layout.deviceIndex);
        }

        CudaReader(CudaReader const&) = delete;
        CudaReader& operator=(CudaReader const&) = delete;

        ~CudaReader()
        {
            if ((_instance != nullptr) && (_reader != nullptr))
            {
                ::mxlReleaseFlowReader(_instance, _reader);
            }
            if (_instance != nullptr)
            {
                ::mxlDestroyInstance(_instance);
            }
        }

        [[nodiscard]]
        std::string const& definition() const noexcept
        {
            return _definition;
        }

        [[nodiscard]]
        mxlRational const& rate() const noexcept
        {
            return _config.common.grainRate;
        }

        enum class Take
        {
            Ready,
            Invalid,
            Early,
            Late,
        };

        struct Grain
        {
            Take kind{Take::Early};
            void* device{nullptr};
            std::uint32_t size{0};
        };

        /**
         * Wait briefly for one grain and return its device pointer.
         * \param[in] index The grain index.
         * \return Ready with a device pointer, Invalid when the writer marked the grain unusable, Early or Late when
         *     the index is outside the ring.
         */
        [[nodiscard]]
        Grain take(std::uint64_t index)
        {
            auto info = mxlGrainInfo{};
            auto slot = std::uint32_t{0};
            auto const status = ::mxlFlowReaderGetGrainSlot(_reader, index, MXL_GRAIN_VALID_SLICES_ALL, 50'000'000ULL, &info, &slot);
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
            {
                return {};
            }
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
            {
                return Grain{Take::Late, nullptr, 0};
            }
            requireMxl(status, "mxlFlowReaderGetGrainSlot");
            if ((info.flags & MXL_GRAIN_FLAG_INVALID) != 0)
            {
                return Grain{Take::Invalid, nullptr, 0};
            }
            if (slot >= _slots.size())
            {
                fail("GetGrainSlot returned a slot outside the mapped range.");
            }
            return Grain{Take::Ready, _slots[slot].cuda.pointer, info.grainSize};
        }

    private:
        mxlInstance _instance{nullptr};
        mxlFlowReader _reader{nullptr};
        mxlFlowConfigInfo _config{};
        std::vector<mxlGrainStorage> _slots;
        std::string _definition;
    };

    void run(CudaReader& reader, Display& display)
    {
        display.start();
        auto const& rate = reader.rate();
        auto const baseTime = ::mxlGetTime();
        auto index = ::mxlGetCurrentIndex(&rate);
        if (index > 0)
        {
            index -= 1U;
        }

        while (g_exit_requested == 0)
        {
            ::mxlSleepForNs(::mxlGetNsUntilIndex(index, &rate));
            auto const grain = reader.take(index);
            if (grain.kind == CudaReader::Take::Late)
            {
                auto const head = ::mxlGetCurrentIndex(&rate);
                index = (head > 0) ? head - 1U : head;
                continue;
            }
            if (grain.kind == CudaReader::Take::Early)
            {
                ::mxlSleepForNs(1'000'000ULL);
                continue;
            }
            if (grain.kind == CudaReader::Take::Ready)
            {
                auto const timestamp = ::mxlIndexToTimestamp(&rate, index);
                auto const pts = (timestamp > baseTime) ? timestamp - baseTime : 0ULL;
                display.push(grain.device, grain.size, pts);
            }
            index += 1U;
        }
    }
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, &onSignal);
    std::signal(SIGTERM, &onSignal);

    auto app = CLI::App{"Display a v210 flow whose payload is in CUDA device memory."};
    auto domain = std::string{};
    app.add_option("-d,--domain", domain, "The MXL domain directory")->required()->check(CLI::ExistingDirectory);
    auto flowId = std::string{};
    app.add_option("-v,--video-flow-id", flowId, "The video flow id")->required();
    auto deviceUuid = std::string{};
    app.add_option("--device-uuid", deviceUuid, "CUDA device UUID used to map the payload. Omit it to use the writer's GPU.");
    CLI11_PARSE(app, argc, argv);

    ::gst_init(nullptr, nullptr);
    try
    {
        auto reader = CudaReader{domain, flowId, payloadOptions(deviceUuid)};
        auto flow = json_utils::parseBuffer(reader.definition());
        if (json_utils::getField<std::string>(flow, "media_type") != "video/v210")
        {
            fail("mxl-gst-cuda-sink displays video/v210 only.");
        }
        auto const width = static_cast<std::uint64_t>(json_utils::getField<double>(flow, "frame_width"));
        auto const height = static_cast<std::uint64_t>(json_utils::getField<double>(flow, "frame_height"));
        auto display = Display{width, height, reader.rate()};
        run(reader, display);
    }
    catch (std::exception const& error)
    {
        MXL_ERROR("{}", error.what());
        ::gst_deinit();
        return 1;
    }

    ::gst_deinit();
    return 0;
}
