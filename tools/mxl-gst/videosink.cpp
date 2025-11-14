// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <glib-object.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstclock.h>
#include <gst/gstpipeline.h>
#include <gst/gstsystemclock.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/FlowParser.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

namespace
{
    std::sig_atomic_t volatile g_exit_requested = 0;

    void signal_handler(int)
    {
        g_exit_requested = 1;
    }

    struct GstreamerAudioPipelineConfig
    {
        mxlRational rate;
        std::size_t channelCount;
        std::int64_t offset;
        std::vector<size_t> spkrEnabled;
    };

    struct GstreamerVideoPipelineConfig
    {
        uint64_t frame_width;
        uint64_t frame_height;
        mxlRational frame_rate;
        std::int64_t offset;
    };

    class GstreamerPipeline
    {
    public:
        virtual void start() = 0;
        virtual void pushSample(GstBuffer* buffer, std::uint64_t now) = 0;
    };

    class GstreamerVideoPipeline : public GstreamerPipeline
    {
    public:
        GstreamerVideoPipeline(GstreamerVideoPipelineConfig config)
            : _config(std::move(config))
        {
            setup();
        }

        ~GstreamerVideoPipeline()
        {
            if (_pipeline)
            {
                gst_element_set_state(_pipeline, GST_STATE_NULL);
                gst_object_unref(_pipeline);
            }
            if (_appsrc)
            {
                if (GST_OBJECT_REFCOUNT_VALUE(_appsrc))
                {
                    gst_object_unref(_appsrc);
                }
            }
        }

        void setup()
        {
            auto pipelineDesc = fmt::format(
                "appsrc name=videoappsrc is-live=true ! "
                "video/x-raw,format=v210,width={},height={},framerate={}/{} !"
                "videoconvert ! "
                "videoscale ! "
                "queue ! "
                "autovideosink ts-offset={}",
                _config.frame_width,
                _config.frame_height,
                _config.frame_rate.numerator,
                _config.frame_rate.denominator,
                _config.offset);

            MXL_INFO("Generating following Video gsteamer pipeline -> {}", pipelineDesc);

            GError* error = nullptr;
            _pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
            if (!_pipeline || error)
            {
                MXL_ERROR("Failed to create pipeline: {}", error->message);
                g_error_free(error);
                throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
            }

            _appsrc = gst_bin_get_by_name(GST_BIN(_pipeline), "videoappsrc");
            if (_appsrc == nullptr)
            {
                throw std::runtime_error("Gstreamer: 'videoappsrc' could not be found in the pipeline.");
            }

            g_object_set(G_OBJECT(_appsrc), "format", GST_FORMAT_TIME, nullptr);

            auto clock = gst_pipeline_get_clock(GST_PIPELINE(_pipeline));
            if (!clock)
            {
                throw std::runtime_error("Gstreamer: could not get pipeline clock.");
            }

            g_object_set(G_OBJECT(clock), "clock-type", GST_CLOCK_TYPE_TAI, nullptr);
        }

        void start() final
        {
            // Start playing
            gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            _gstBaseTime = gst_element_get_base_time(_pipeline);
            MXL_INFO("Video pipeline base time: {} ns", _gstBaseTime);
        }

        void pushSample(GstBuffer* buffer, std::uint64_t now) final
        {
            GST_BUFFER_PTS(buffer) = now - _gstBaseTime;

            int ret;
            g_signal_emit_by_name(_appsrc, "push-buffer", buffer, &ret);
            if (ret != GST_FLOW_OK)
            {
                MXL_ERROR("Error pushing buffer to video appsrc");
            }
        }

        GstreamerVideoPipelineConfig _config;

    private:
        GstElement* _pipeline{nullptr};
        GstElement* _appsrc{nullptr};

        std::uint64_t _gstBaseTime{0};
    };

    class GstreamerAudioPipeline : public GstreamerPipeline
    {
    public:
        GstreamerAudioPipeline(GstreamerAudioPipelineConfig config)
            : _config(std::move(config))
        {
            setup();
        }

        ~GstreamerAudioPipeline()
        {
            if (_audioInfo)
            {
                gst_audio_info_free(_audioInfo);
            }

            if (_pipeline)
            {
                gst_element_set_state(_pipeline, GST_STATE_NULL);
                gst_object_unref(_pipeline);
            }

            if (_appsrc)
            {
                if (GST_OBJECT_REFCOUNT_VALUE(_appsrc))
                {
                    gst_object_unref(_appsrc);
                }
            }
        }

        void setup()
        {
            auto mixMatrix = generateMixMatrix();
            MXL_INFO("Mix matrix: {}", mixMatrix);

            std::string pipelineDesc = fmt::format(
                "appsrc name=audioappsrc is-live=true ! "
                "audio/x-raw,format=F32LE,layout=non-interleaved,channels={},rate=48000 ! "
                "audioconvert mix-matrix={} !"
                "autoaudiosink ts-offset={}",
                _config.channelCount,
                generateMixMatrix(),
                _config.offset);

            MXL_INFO("Generating following Audio gsteamer pipeline -> {}", pipelineDesc);

            GError* error = nullptr;
            _pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
            if (!_pipeline || error)
            {
                MXL_ERROR("Failed to create pipeline: {}", error->message);
                g_error_free(error);
                throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
            }

            _appsrc = gst_bin_get_by_name(GST_BIN(_pipeline), "audioappsrc");
            if (_appsrc == nullptr)
            {
                throw std::runtime_error("Gstreamer: 'audioappsrc' could not be found in the pipeline.");
            }

            auto caps = gstCapsFromAudioConfig(_config);
            g_object_set(G_OBJECT(_appsrc), "caps", caps, "format", GST_FORMAT_TIME, nullptr);

            auto clock = gst_pipeline_get_clock(GST_PIPELINE(_pipeline));
            if (!clock)
            {
                throw std::runtime_error("Gstreamer: could not get pipeline clock.");
            }

            g_object_set(G_OBJECT(clock), "clock-type", GST_CLOCK_TYPE_TAI, nullptr);
        }

        std::string generateMixMatrix()
        {
            std::string out = "\"< ";
            for (size_t spkrId = 0; spkrId < _config.spkrEnabled.size(); spkrId++)
            {
                auto spkrEn = _config.spkrEnabled[spkrId];

                out += "< ";
                for (size_t chan = 0; chan < _config.channelCount; chan++)
                {
                    if (chan == spkrEn)
                    {
                        out += "(float)1";
                    }
                    else
                    {
                        out += "(float)0";
                    }

                    if (chan != _config.channelCount - 1)
                    {
                        out += ", ";
                    }
                }
                out += " >";

                if (spkrId != _config.spkrEnabled.size() - 1)
                {
                    out += ", ";
                }
            }
            out += " >\" ";

            return out;
        }

        GstCaps* gstCapsFromAudioConfig(GstreamerAudioPipelineConfig const& config)
        {
            _audioInfo = gst_audio_info_new();

            std::vector<GstAudioChannelPosition> chanPositions;
            for (size_t i = 0; i < config.channelCount; i++)
            {
                chanPositions.emplace_back(static_cast<GstAudioChannelPosition>(i));
            }

            gst_audio_info_set_format(_audioInfo, GST_AUDIO_FORMAT_F32LE, config.rate.numerator, config.channelCount, chanPositions.data());
            _audioInfo->layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;

            auto* caps = gst_audio_info_to_caps(_audioInfo);
            return caps;
        }

        void start() final
        {
            // Start playing
            gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            _gstBaseTime = gst_element_get_base_time(_pipeline);
            MXL_INFO("Audio pipeline base time: {} ns", _gstBaseTime);
        }

        void pushSample(GstBuffer* buffer, std::uint64_t now) final
        {
            GST_BUFFER_PTS(buffer) = now - _gstBaseTime;

            int ret;
            g_signal_emit_by_name(_appsrc, "push-buffer", buffer, &ret);
            if (ret != GST_FLOW_OK)
            {
                MXL_ERROR("Error pushing buffer to video appsrc");
            }
        }

        GstAudioInfo* _audioInfo{nullptr};
        GstreamerAudioPipelineConfig _config;

    private:
        GstElement* _pipeline{nullptr};
        GstElement* _appsrc{nullptr};

        std::uint64_t _gstBaseTime{0};
    };

    class MxlReader
    {
    public:
        MxlReader(std::string const& domain, std::string flowId)
        {
            _instance = mxlCreateInstance(domain.c_str(), "");
            if (_instance == nullptr)
            {
                throw std::runtime_error("Failed to create MXL instance");
            }

            auto ret = mxlCreateFlowReader(_instance, flowId.c_str(), "", &_reader);
            if (ret != MXL_STATUS_OK)
            {
                throw std::runtime_error(fmt::format("Failed to create MXL Flow Reader with error status \"{}\"", static_cast<int>(ret)));
            }
            ret = mxlFlowReaderGetInfo(_reader, &_flowInfo);
            if (ret != MXL_STATUS_OK)
            {
                throw std::runtime_error(fmt::format("Failed to get MXL Flow Info with error status \"{}\"", static_cast<int>(ret)));
            }
        }

        ~MxlReader()
        {
            if (_reader != nullptr)
            {
                mxlReleaseFlowReader(_instance, _reader);
            }
            if (_instance != nullptr)
            {
                mxlDestroyInstance(_instance);
            }
        }

        int run(GstreamerPipeline& gstPipeline, std::int64_t readDelay, bool timeoutMode)
        {
            gstPipeline.start();

            if (mxlIsDiscreteDataFormat(_flowInfo.config.common.format))
            {
                return runDiscreteFlow(dynamic_cast<GstreamerVideoPipeline&>(gstPipeline), readDelay, timeoutMode);
            }
            else
            {
                return runContinuousFlow(dynamic_cast<GstreamerAudioPipeline&>(gstPipeline), readDelay);
            }
        }

        int runDiscreteFlow(GstreamerVideoPipeline& gstPipeline, std::int64_t readDelay, bool timeoutMode)
        {
            auto rate = _flowInfo.config.common.grainRate;
            auto slicesPerBatch = _flowInfo.config.common.maxSyncBatchSizeHint;
            if (slicesPerBatch > gstPipeline._config.frame_height)
            {
                throw std::invalid_argument{"slicesPerBatch cannot be greater than frame height."};
            }

            auto sliceReadMode = slicesPerBatch < gstPipeline._config.frame_height;

            MXL_INFO("Starting discrete flow reading at rate {}/{} and slices per batch {}", rate.numerator, rate.denominator, slicesPerBatch);
            auto timeoutNs = discreteFlowTimeout(timeoutMode, gstPipeline._config.offset);

            auto index = mxlGetCurrentIndex(&rate);
            mxlStatus ret;
            std::uint16_t validSlices = slicesPerBatch;
            while (!g_exit_requested)
            {
                mxlGrainInfo grainInfo;
                uint8_t* payload;

                auto requestedIndex = requestIndex(index, readDelay, timeoutMode);
                if (sliceReadMode)
                {
                    // Slice mode is not really useful here since gstreamer needs the full grain to push to the pipeline. But for educational
                    // purposes, here's how you can wait for slices to be available.
                    ret = mxlFlowReaderGetGrainSlice(_reader, requestedIndex, validSlices, timeoutNs, &grainInfo, &payload);
                    validSlices = std::min<std::uint16_t>(gstPipeline._config.frame_height,
                        grainInfo.validSlices + slicesPerBatch); // calculate up to how many valid slices to wait for the next iteration
                }
                else
                {
                    // Use this function to wait for a full grain
                    ret = mxlFlowReaderGetGrain(_reader, requestedIndex, timeoutNs, &grainInfo, &payload);
                }

                if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                {
                    // We are too early somehow, keep trying the same grain index
                    mxlFlowReaderGetInfo(_reader, &_flowInfo);
                    MXL_WARN("Failed to get samples at index {}: TOO EARLY. Last published {}", requestedIndex, _flowInfo.runtime.headIndex);

                    continue;
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
                {
                    mxlFlowReaderGetInfo(_reader, &_flowInfo);
                    MXL_WARN("Failed to get grain at index {}: TOO LATE. Last published {}", requestedIndex, _flowInfo.runtime.headIndex);

                    // Grain expired. Realign to current index. GStreamer repeats the last valid frame for missing data; consuming applications
                    // should do the same.
                    index = mxlGetCurrentIndex(&rate);
                    continue;
                }
                else if (ret == MXL_ERR_TIMEOUT)
                {
                    // Timeout waiting for grain
                    MXL_WARN("Timeout waiting for grain at index {}", requestedIndex);
                    continue;
                }
                else if (ret != MXL_STATUS_OK)
                {
                    MXL_ERROR("Unexpected error when reading the grain {} with status '{}'. Exiting.", requestedIndex, static_cast<int>(ret));
                    return ret;
                }

                if (grainInfo.validSlices != grainInfo.totalSlices)
                {
                    // Full frame not ready yet
                    continue;
                }

                if (!(grainInfo.flags & MXL_GRAIN_FLAG_INVALID))
                {
                    // We've validated the grain. Invalid grains are skipped rather than pushed to GStreamer. Since we provide PTS values based on MXL
                    // timestamps, GStreamer automatically handles missing grains by repeating the last valid frame. Consuming applications should
                    // implement similar logic for invalid grain handling.                    if (timeoutMode)

                    updateHighestLatency(mxlIndexToTimestamp(&rate, requestedIndex), mxlGetTime());

                    // If we got here, we can push the grain to the gstreamer pipeline
                    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, grainInfo.grainSize, nullptr);
                    GstMapInfo map;
                    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
                    ::memcpy(map.data, payload, grainInfo.grainSize);
                    gst_buffer_unmap(buffer, &map);

                    gstPipeline.pushSample(buffer, mxlIndexToTimestamp(&rate, index));

                    gst_buffer_unref(buffer);
                }

                index++;
                validSlices = slicesPerBatch;
                mxlSleepForNs(mxlGetNsUntilIndex(index, &rate));
            }

            return 0;
        }

        int runContinuousFlow(GstreamerAudioPipeline& gstPipeline, std::int64_t readDelay)
        {
            auto rate = _flowInfo.config.common.grainRate;
            auto const windowSize = _flowInfo.config.common.maxSyncBatchSizeHint; // samples per read
            MXL_INFO("Starting continuous flow reading at rate {}/{} and batch size {}", rate.numerator, rate.denominator, windowSize);

            mxlWrappedMultiBufferSlice payload;

            auto index = mxlGetCurrentIndex(&rate);
            while (!g_exit_requested)
            {
                auto requestedIndex = index - readDelay;

                auto ret = mxlFlowReaderGetSamplesNonBlocking(_reader, requestedIndex, windowSize, &payload);
                if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                {
                    // We are too early somehow, keep trying the same index
                    mxlFlowReaderGetInfo(_reader, &_flowInfo);
                    MXL_WARN("Failed to get samples at index {}: TOO EARLY. Last published {}", requestedIndex, _flowInfo.runtime.headIndex);
                    continue;
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
                {
                    // Samples expired. Realign to current index. GStreamer will generate silence for missing samples. Consuming applications
                    // should handle this better by inserting silence with a micro fades to prevent clicks and pops.
                    mxlFlowReaderGetInfo(_reader, &_flowInfo);
                    MXL_WARN("Failed to get samples at index {}: TOO LATE. Last published {}", requestedIndex, _flowInfo.runtime.headIndex);

                    index = mxlGetCurrentIndex(&rate);
                    continue;
                }
                else if (ret == MXL_ERR_TIMEOUT)
                {
                    // Timeout waiting for samples
                    MXL_WARN("Timeout waiting for samples at index {}", requestedIndex);
                    continue;
                }
                else if (ret != MXL_STATUS_OK)
                {
                    MXL_ERROR("Unexpected error when reading the samples {} with status '{}'. Exiting.", requestedIndex, static_cast<int>(ret));
                    return ret;
                }

                auto payloadLen = windowSize * payload.count * sizeof(float);
                GstBuffer* buffer{gst_buffer_new_allocate(nullptr, payloadLen, nullptr)};

                GstAudioMeta* audioMeta = gst_buffer_add_audio_meta(buffer, gstPipeline._audioInfo, windowSize, nullptr);
                if (!audioMeta)
                {
                    MXL_ERROR("Error while adding meta to audio buffer.");
                    gst_buffer_unref(buffer);
                    continue;
                }
                GstAudioBuffer audioBuffer;
                if (!gst_audio_buffer_map(&audioBuffer, gstPipeline._audioInfo, buffer, GST_MAP_WRITE))
                {
                    MXL_ERROR("Error while mapping audio buffer.");
                    gst_buffer_unref(buffer);
                    continue;
                }

                for (std::size_t channel = 0; channel < payload.count; ++channel)
                {
                    auto currentWritePtr{static_cast<std::byte*>(audioBuffer.planes[channel])};
                    auto const readPtr0{static_cast<std::byte const*>(payload.base.fragments[0].pointer) + channel * payload.stride};
                    auto const readSize0{payload.base.fragments[0].size};
                    ::memcpy(currentWritePtr, readPtr0, readSize0);
                    currentWritePtr += readSize0;

                    auto const readPtr1{static_cast<std::byte const*>(payload.base.fragments[1].pointer) + channel * payload.stride};
                    auto const readSize1{payload.base.fragments[1].size};
                    ::memcpy(currentWritePtr, readPtr1, readSize1);
                }

                gst_audio_buffer_unmap(&audioBuffer);

                gstPipeline.pushSample(buffer, mxlIndexToTimestamp(&rate, index));

                gst_buffer_unref(buffer);

                index += windowSize;
                mxlSleepForNs(mxlGetNsUntilIndex(index, &rate));
            }

            return 0;
        }

    private:
        std::uint64_t discreteFlowTimeout(bool timeoutMode, std::int64_t offset)
        {
            auto rate = _flowInfo.config.common.grainRate;

            if (timeoutMode)
            {
                // With a negative offset, grains should be ready right away
                return offset > 0 ? static_cast<std::uint64_t>(offset) : 0;
            }
            else
            {
                return static_cast<std::uint64_t>(1.0 * rate.denominator * (1'000'000'000.0 / rate.numerator) + 1'000'000ULL);
            }
        }

        std::uint64_t requestIndex(std::uint64_t index, std::uint64_t readDelay, bool timeoutMode)
        {
            return timeoutMode ? index : index - readDelay;
        }

        void updateHighestLatency(std::uint64_t mxlTimestamp, std::uint64_t currentMxlTime)
        {
            auto const latencyNs = currentMxlTime > mxlTimestamp ? currentMxlTime - mxlTimestamp : 0;
            if (latencyNs > _highestLatencyNs)
            {
                _highestLatencyNs = latencyNs;
                MXL_INFO("Latency increase detected: {} ns", latencyNs);
            }
        }

    private:
        mxlFlowInfo _flowInfo;
        mxlInstance _instance;
        mxlFlowReader _reader;
        std::uint64_t _highestLatencyNs = 0;
    };

    std::string read_flow_descriptor(std::string const& domain, std::string const& flowID)
    {
        auto const descriptor_path = mxl::lib::makeFlowDescriptorFilePath(domain, flowID);
        if (!std::filesystem::exists(descriptor_path))
        {
            throw std::runtime_error{fmt::format("Flow descriptor file '{}' does not exist.", descriptor_path.string())};
        }

        std::ifstream descriptor_reader(descriptor_path);
        return std::string{(std::istreambuf_iterator<char>(descriptor_reader)), std::istreambuf_iterator<char>()};
    }

    int real_main(int argc, char** argv, void*)
    {
        std::signal(SIGINT, &signal_handler);
        std::signal(SIGTERM, &signal_handler);

        CLI::App app("mxl-gst-videosink");

        std::string videoFlowID;
        app.add_option("-v, --video-flow-id", videoFlowID, "The video flow ID");

        std::string audioFlowID;
        app.add_option("-a, --audio-flow-id", audioFlowID, "The audio flow ID");

        std::string domain;
        auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
        domainOpt->required(true);
        domainOpt->check(CLI::ExistingDirectory);

        std::vector<std::size_t> listenChannels;
        auto listenChanOpt = app.add_option("-l, --listen-channels", listenChannels, "Audio channels to listen");
        listenChanOpt->default_val(std::vector<std::size_t>{0, 1});

        std::int64_t audioOffset;
        auto audioOffsetOpt = app.add_option(
            "--audio-offset", audioOffset, "Audio playback offset in nanoseconds. Positive value means you are delaying the playback");
        audioOffsetOpt->default_val(150'000'000);

        std::int64_t videoOffset;
        auto videoOffsetOpt = app.add_option(
            "--video-offset", videoOffset, "Video plaback offset in nanoseconds. Positive value means you are delaying the playback");
        videoOffsetOpt->default_val(150'000'000);

        std::int64_t audioReadDelay;
        auto audioReadDelayOpt = app.add_option("--audio-read-delay",
            audioReadDelay,
            "How far in the past/future to read (in audio samples). Positive value means you are delaying the read");
        audioReadDelayOpt->default_val(768);

        std::int64_t videoReadDelay;
        auto videoReadDelayOpt = app.add_option("--video-read-delay",
            videoReadDelay,
            "How far in the past/future to read (in video frames). Positive value means you are delaying the read");
        videoReadDelayOpt->default_val(1);

        bool timeoutMode;
        auto timeoutModeOpt = app.add_flag("--timeout-mode",
            timeoutMode,
            "In timeout mode, the sink uses blocking methods with timeout instead of reading with a defined delay. Currently only video supports "
            "this mode and --video-offset is used for both the timeout value and playback offset.");
        timeoutModeOpt->default_val(false);

        CLI11_PARSE(app, argc, argv);

        gst_init(nullptr, nullptr);

        std::vector<std::thread> threads;

        if (!videoFlowID.empty())
        {
            threads.emplace_back(
                [&]()
                {
                    auto reader = MxlReader(domain, videoFlowID);

                    auto flowDescriptor = read_flow_descriptor(domain, videoFlowID);
                    mxl::lib::FlowParser parser{flowDescriptor};

                    if (parser.get<std::string>("interlace_mode") != "progressive")
                    {
                        throw std::invalid_argument{"This application does not support interlaced flows."};
                    }

                    GstreamerVideoPipelineConfig videoConfig{
                        .frame_width = static_cast<std::uint64_t>(parser.get<double>("frame_width")),
                        .frame_height = static_cast<std::uint64_t>(parser.get<double>("frame_height")),
                        .frame_rate = parser.getGrainRate(),
                        .offset = videoOffset,
                    };

                    auto pipeline = GstreamerVideoPipeline(videoConfig);
                    reader.run(pipeline, videoReadDelay, timeoutMode);

                    MXL_INFO("Video pipeline finished");
                    return 0;
                });
        }

        if (!audioFlowID.empty())
        {
            threads.emplace_back(
                [&]()
                {
                    auto reader = MxlReader(domain, audioFlowID);

                    auto flowDescriptor = read_flow_descriptor(domain, audioFlowID);
                    mxl::lib::FlowParser parser{flowDescriptor};

                    GstreamerAudioPipelineConfig audioConfig{
                        .rate = parser.getGrainRate(),
                        .channelCount = parser.getChannelCount(),
                        .offset = audioOffset,
                        .spkrEnabled = listenChannels,
                    };

                    auto pipeline = GstreamerAudioPipeline(audioConfig);
                    reader.run(pipeline, audioReadDelay, false);

                    MXL_INFO("Audio pipeline finished");
                    return 0;
                });
        }

        for (auto& t : threads)
        {
            t.join();
        }
        gst_deinit();

        return 0;
    }

}

int main(int argc, char* argv[])

{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
    // macOS needs an NSApp event loop.  This gst function sets it up.
    return gst_macos_main((GstMainFunc)real_main, argc, argv, nullptr);
#else
    return real_main(argc, argv, nullptr);
#endif
}
