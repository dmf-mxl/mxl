// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file cuda_testsrc.cpp
 *
 * Publish a v210 test picture whose payload is in CUDA device memory.
 * videotestsrc produces host frames. Each frame is copied with cudaMemcpy into the slot returned by
 * mxlFlowWriterOpenGrainSlot, and that copy is finished with cudaDeviceSynchronize before
 * mxlFlowWriterCommitGrain. The cuda-linear backend stores video only, so this tool does not publish audio.
 */

#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <cuda_runtime_api.h>
#include <fmt/format.h>
#include <gst/app/gstappsink.h>
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
        if (deviceUuid.find_first_of("\\\"") != std::string::npos)
        {
            fail("device UUID contains a character that cannot be placed in the writer options");
        }
        return fmt::format(R"({{"payload":{{"backend":"cuda-linear","deviceUuid":"{}"}}}})", deviceUuid);
    }

    class Sample
    {
    public:
        Sample() noexcept = default;

        Sample(GstSample* sample, GstBuffer* buffer) noexcept
            : _sample{sample}
            , _buffer{buffer}
        {}

        Sample(Sample const&) = delete;
        Sample& operator=(Sample const&) = delete;

        Sample(Sample&& other) noexcept
            : _sample{other._sample}
            , _buffer{other._buffer}
        {
            other._sample = nullptr;
            other._buffer = nullptr;
        }

        Sample& operator=(Sample&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                _sample = other._sample;
                _buffer = other._buffer;
                other._sample = nullptr;
                other._buffer = nullptr;
            }
            return *this;
        }

        ~Sample()
        {
            reset();
        }

        [[nodiscard]]
        explicit operator bool() const noexcept
        {
            return (_sample != nullptr) && (_buffer != nullptr);
        }

        [[nodiscard]]
        GstBuffer* buffer() const noexcept
        {
            return _buffer;
        }

    private:
        void reset() noexcept
        {
            if (_buffer != nullptr)
            {
                ::gst_buffer_unref(_buffer);
                _buffer = nullptr;
            }
            if (_sample != nullptr)
            {
                ::gst_sample_unref(_sample);
                _sample = nullptr;
            }
        }

        GstSample* _sample{nullptr};
        GstBuffer* _buffer{nullptr};
    };

    class VideoSource
    {
    public:
        VideoSource(std::uint64_t width, std::uint64_t height, mxlRational rate, std::uint64_t pattern, std::string const& overlay)
            : _rate{rate}
        {
            auto const description = fmt::format(
                "videotestsrc name=videotestsrc is-live=true do-timestamp=true pattern={} ! "
                "video/x-raw,format=v210,width={},height={},framerate={}/{} ! "
                "textoverlay text=\"{}\" font-desc=\"Sans, 36\" ! "
                "clockoverlay ! videoconvert ! videoscale ! queue ! appsink name=appsink",
                pattern,
                width,
                height,
                rate.numerator,
                rate.denominator,
                overlay);
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

            _appSink = ::gst_bin_get_by_name(GST_BIN(_pipeline), "appsink");
            if (_appSink == nullptr)
            {
                fail("The pipeline has no appsink.");
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
            ::g_object_set(G_OBJECT(_appSink), "caps", caps, nullptr);
            ::gst_caps_unref(caps);
        }

        VideoSource(VideoSource const&) = delete;
        VideoSource& operator=(VideoSource const&) = delete;

        ~VideoSource()
        {
            if (_appSink != nullptr)
            {
                ::gst_object_unref(_appSink);
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
                fail("Failed to start the GStreamer pipeline.");
            }
            // Align the pipeline clock to the next grain so buffer timestamps map onto grain indices.
            _baseTime = ::mxlIndexToTimestamp(&_rate, ::mxlGetCurrentIndex(&_rate) + 1U);
            MXL_INFO("Pipeline base time: {} ns", _baseTime);
        }

        [[nodiscard]]
        Sample pull(std::uint64_t timeoutNs)
        {
            auto* sample = ::gst_app_sink_try_pull_sample(GST_APP_SINK(_appSink), timeoutNs);
            if (sample == nullptr)
            {
                return {};
            }
            auto* buffer = ::gst_sample_get_buffer(sample);
            if (buffer == nullptr)
            {
                ::gst_sample_unref(sample);
                return {};
            }
            ::gst_buffer_ref(buffer);
            auto const pts = GST_BUFFER_PTS(buffer);
            GST_BUFFER_PTS(buffer) = _baseTime + pts;
            return Sample{sample, buffer};
        }

        [[nodiscard]]
        mxlRational const& rate() const noexcept
        {
            return _rate;
        }

    private:
        mxlRational _rate;
        GstElement* _pipeline{nullptr};
        GstElement* _appSink{nullptr};
        std::uint64_t _baseTime{0};
    };

    class CudaWriter
    {
    public:
        CudaWriter(std::string const& domain, std::string const& flowDef, std::string const& options)
        {
            _instance = ::mxlCreateInstance(domain.c_str(), "");
            if (_instance == nullptr)
            {
                fail("Failed to create an MXL instance.");
            }
            auto created = false;
            requireMxl(::mxlCreateFlowWriter(_instance, flowDef.c_str(), options.c_str(), &_writer, &_config, &created), "mxlCreateFlowWriter");
            if (_config.common.format != MXL_DATA_FORMAT_VIDEO)
            {
                fail("cuda-linear stores video flows only.");
            }

            auto layout = mxlGrainStorageLayout{};
            requireMxl(::mxlFlowWriterGetStorageLayout(_writer, &layout), "mxlFlowWriterGetStorageLayout");
            if (layout.storageType != MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER)
            {
                fail("The flow payload is not in CUDA device memory.");
            }
            _slots.resize(layout.slotCount);
            requireMxl(::mxlFlowWriterMapSlots(_writer, layout.slotCount, _slots.data()), "mxlFlowWriterMapSlots");
            requireCuda(::cudaSetDevice(layout.deviceIndex), "cudaSetDevice");
            MXL_INFO("Payload is on CUDA device {}", layout.deviceIndex);
        }

        CudaWriter(CudaWriter const&) = delete;
        CudaWriter& operator=(CudaWriter const&) = delete;

        ~CudaWriter()
        {
            if ((_instance != nullptr) && (_writer != nullptr))
            {
                ::mxlReleaseFlowWriter(_instance, _writer);
            }
            if (_instance != nullptr)
            {
                ::mxlDestroyInstance(_instance);
            }
        }

        [[nodiscard]]
        mxlRational const& rate() const noexcept
        {
            return _config.common.grainRate;
        }

        void commit(std::uint64_t index, void const* source, std::size_t sourceSize, bool invalid)
        {
            auto info = mxlGrainInfo{};
            auto slot = std::uint32_t{0};
            requireMxl(::mxlFlowWriterOpenGrainSlot(_writer, index, &info, &slot), "mxlFlowWriterOpenGrainSlot");
            if (invalid)
            {
                info.flags = MXL_GRAIN_FLAG_INVALID;
            }
            else
            {
                if (slot >= _slots.size())
                {
                    fail("OpenGrainSlot returned a slot outside the mapped range.");
                }
                auto const bytes = std::min(sourceSize, static_cast<std::size_t>(info.grainSize));
                requireCuda(::cudaMemcpy(_slots[slot].cuda.pointer, source, bytes, cudaMemcpyHostToDevice), "cudaMemcpy");
                // The commit publishes the grain. Device writes have to be visible before that.
                requireCuda(::cudaDeviceSynchronize(), "cudaDeviceSynchronize");
                info.validSlices = info.totalSlices;
            }
            requireMxl(::mxlFlowWriterCommitGrain(_writer, &info), "mxlFlowWriterCommitGrain");
        }

    private:
        mxlInstance _instance{nullptr};
        mxlFlowWriter _writer{nullptr};
        mxlFlowConfigInfo _config{};
        std::vector<mxlGrainStorage> _slots;
    };

    void run(CudaWriter& writer, VideoSource& source)
    {
        source.start();
        auto grainIndex = std::uint64_t{MXL_UNDEFINED_INDEX};
        while (g_exit_requested == 0)
        {
            auto const timeoutNs =
                (grainIndex != MXL_UNDEFINED_INDEX) ? ::mxlGetNsUntilIndex(grainIndex + 1U, &source.rate()) : 100'000'000ULL;
            auto sample = source.pull(timeoutNs);
            if (!sample)
            {
                continue;
            }

            auto const bufferTs = GST_BUFFER_PTS(sample.buffer());
            auto const gstIndex = ::mxlTimestampToIndex(&source.rate(), bufferTs);
            if (grainIndex == MXL_UNDEFINED_INDEX)
            {
                grainIndex = gstIndex;
                MXL_INFO("First grain index is {}", grainIndex);
            }
            if (gstIndex < grainIndex)
            {
                MXL_ERROR("GStreamer timestamp moved backward: got index {}, expected {}", gstIndex, grainIndex);
                break;
            }
            if (gstIndex > grainIndex)
            {
                MXL_WARN("Missed {} grain(s) before index {}", gstIndex - grainIndex, gstIndex);
                while ((grainIndex < gstIndex) && (g_exit_requested == 0))
                {
                    writer.commit(grainIndex, nullptr, 0, true);
                    grainIndex += 1U;
                }
            }

            auto map = GstMapInfo{};
            if (!::gst_buffer_map(sample.buffer(), &map, GST_MAP_READ))
            {
                MXL_ERROR("Failed to map the GStreamer buffer for grain {}", grainIndex);
                break;
            }
            writer.commit(grainIndex, map.data, map.size, false);
            ::gst_buffer_unmap(sample.buffer(), &map);
            grainIndex += 1U;
            ::mxlSleepForNs(::mxlGetNsUntilIndex(grainIndex, &writer.rate()));
        }
    }

    auto const patternIds = std::unordered_map<std::string, std::uint64_t>{
        {"smpte", 0},
        {"snow", 1},
        {"black", 2},
        {"white", 3},
        {"red", 4},
        {"green", 5},
        {"blue", 6},
        {"checkers-1", 7},
        {"checkers-2", 8},
        {"ball", 18},
    };
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, &onSignal);
    std::signal(SIGTERM, &onSignal);

    auto app = CLI::App{"Publish a v210 test picture in CUDA device memory."};
    auto domain = std::string{};
    app.add_option("-d,--domain", domain, "The MXL domain directory")->required()->check(CLI::ExistingDirectory);
    auto flowFile = std::string{};
    app.add_option("-v,--video-config-file", flowFile, "NMOS video flow JSON")->required()->check(CLI::ExistingFile);
    auto deviceUuid = std::string{};
    app.add_option("--device-uuid", deviceUuid, "CUDA device UUID, as printed by nvidia-smi -L")->required();
    auto pattern = std::string{"smpte"};
    app.add_option("-p,--pattern", pattern, "videotestsrc pattern name");
    auto overlay = std::string{"EBU DMF MXL"};
    app.add_option("-t,--overlay-text", overlay, "Text drawn on the test picture");
    auto groupHint = std::string{"mxl-gst-cuda-testsrc"};
    app.add_option("-g,--group-hint", groupHint, "NMOS group hint stored in the flow definition");
    CLI11_PARSE(app, argc, argv);

    ::gst_init(nullptr, nullptr);
    try
    {
        auto patternId = patternIds.find(pattern);
        if (patternId == patternIds.end())
        {
            fail("Unknown videotestsrc pattern: " + pattern);
        }

        auto flow = json_utils::parseFile(flowFile);
        if (json_utils::getField<std::string>(flow, "format") != "urn:x-nmos:format:video")
        {
            fail("The flow is not a video flow.");
        }
        if (json_utils::getField<std::string>(flow, "media_type") != "video/v210")
        {
            fail("mxl-gst-cuda-testsrc publishes video/v210 only.");
        }
        if (json_utils::getField<std::string>(flow, "interlace_mode") != "progressive")
        {
            fail("Interlaced video is not supported.");
        }
        json_utils::updateGroupHint(flow, groupHint, "video");

        auto const width = static_cast<std::uint64_t>(json_utils::getField<double>(flow, "frame_width"));
        auto const height = static_cast<std::uint64_t>(json_utils::getField<double>(flow, "frame_height"));
        auto const rate = json_utils::getRational(flow, "grain_rate");
        auto writer = CudaWriter{domain, json_utils::serializeJson(flow), payloadOptions(deviceUuid)};
        auto source = VideoSource{width, height, rate, patternId->second, overlay};
        run(writer, source);
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
