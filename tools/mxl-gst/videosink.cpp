// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <glib-object.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
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
        std::size_t nbSamplesPerBatch;
        std::vector<size_t> spkrEnabled;
    };

    struct GstreamerVideoPipelineConfig
    {
        uint64_t frame_width;
        uint64_t frame_height;
        mxlRational frame_rate;
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
                "appsrc name=videoappsrc ! "
                "video/x-raw,format=v210,width={},height={},framerate={}/{} !"
                "videoconvert ! "
                "videoscale ! "
                "queue ! "
                "autovideosink",
                _config.frame_width,
                _config.frame_height,
                _config.frame_rate.numerator,
                _config.frame_rate.denominator);

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
                throw std::runtime_error("Gstreamer: 'appsrc' could not be found in the pipeline.");
            }

            g_object_set(G_OBJECT(_appsrc), "format", GST_FORMAT_TIME, nullptr);
        }

        void start() final
        {
            // Start playing
            gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            auto baseTime = gst_element_get_base_time(_pipeline);
            _mxlClockOffset = mxlGetTime() - baseTime;
            MXL_INFO("Video pipeline base time: {} ns, MXL clock offset: {} ns", baseTime, _mxlClockOffset);
        }

        void pushSample(GstBuffer* buffer, std::uint64_t now) final
        {
            GST_BUFFER_PTS(buffer) = now - _mxlClockOffset;

            int ret;
            g_signal_emit_by_name(_appsrc, "push-buffer", buffer, &ret);
            if (ret != GST_FLOW_OK)
            {
                MXL_ERROR("Error pushing buffer to video appsrc");
            }
        }

    private:
        GstreamerVideoPipelineConfig _config;

        GstElement* _pipeline{nullptr};
        GstElement* _appsrc{nullptr};

        std::uint64_t _mxlClockOffset{0};
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
                "appsrc name=audioappsrc ! "
                "audio/x-raw,format=F32LE,layout=non-interleaved,channels={},rate=48000 ! "
                "audioconvert mix-matrix={} !"
                "autoaudiosink",
                _config.channelCount,
                generateMixMatrix());

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
                throw std::runtime_error("Gstreamer: 'appsink' could not be found in the pipeline.");
            }

            auto caps = gstCapsFromAudioConfig(_config);
            g_object_set(G_OBJECT(_appsrc), "caps", caps, "format", GST_FORMAT_TIME, nullptr);
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
            auto baseTime = gst_element_get_base_time(_pipeline);
            _mxlClockOffset = mxlGetTime() - baseTime;
            MXL_INFO("Audio pipeline base time: {} ns, MXL clock offset: {} ns", baseTime, _mxlClockOffset);
        }

        void pushSample(GstBuffer* buffer, std::uint64_t now) final
        {
            GST_BUFFER_PTS(buffer) = now - _mxlClockOffset;

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

        std::uint64_t _mxlClockOffset{0};
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

        int run(GstreamerPipeline& gstPipeline, std::int64_t playbackOffset)
        {
            gstPipeline.start();

            if (mxlIsDiscreteDataFormat(_flowInfo.common.format))
            {
                return runDiscreteFlow(dynamic_cast<GstreamerVideoPipeline&>(gstPipeline), _flowInfo.discrete.grainRate, playbackOffset);
            }
            else
            {
                return runContinuousFlow(dynamic_cast<GstreamerAudioPipeline&>(gstPipeline), _flowInfo.continuous.sampleRate, playbackOffset);
            }
        }

        int runDiscreteFlow(GstreamerVideoPipeline& gstPipeline, mxlRational const& rate, std::int64_t playbackOffset)
        {
            MXL_INFO("Starting discrete flow reading at rate {}/{}", rate.numerator, rate.denominator);

            auto timeoutNs = static_cast<std::uint64_t>(1.0 * rate.denominator * (1'000'000'000.0 / rate.numerator) + 1'000'000ULL);
            auto index = mxlGetCurrentIndex(&rate);
            while (!g_exit_requested)
            {
                mxlGrainInfo grainInfo;
                uint8_t* payload;
                auto ret = mxlFlowReaderGetGrain(_reader, index - playbackOffset, timeoutNs, &grainInfo, &payload);
                if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                {
                    // We are too early somehow, keep trying the same grain index
                    mxlFlowReaderGetInfo(_reader, &_flowInfo);
                    MXL_WARN("Failed to get samples at index {}: TOO EARLY. Last published {}", index, _flowInfo.discrete.headIndex);

                    continue;
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
                {
                    // We are too late, that's too bad. Time travel!
                    index = mxlGetCurrentIndex(&rate);
                    continue;
                }
                else if (ret != MXL_STATUS_OK)
                {
                    MXL_ERROR("Unexpected error when reading the grain {} with status '{}'. Exiting.", index, static_cast<int>(ret));
                    return ret;
                }

                if (grainInfo.validSlices != grainInfo.totalSlices)
                {
                    // Full frame not ready yet
                    continue;
                }

                // If we got here, we can push the grain to the gstreamer pipeline
                GstBuffer* buffer = gst_buffer_new_allocate(nullptr, grainInfo.grainSize, nullptr);
                GstMapInfo map;
                gst_buffer_map(buffer, &map, GST_MAP_WRITE);
                ::memcpy(map.data, payload, grainInfo.grainSize);
                gst_buffer_unmap(buffer, &map);

                gstPipeline.pushSample(buffer, mxlIndexToTimestamp(&rate, index));

                gst_buffer_unref(buffer);
            }

            return 0;
        }

        int runContinuousFlow(GstreamerAudioPipeline& gstPipeline, mxlRational const& rate, std::int64_t playbackOffset)
        {
            MXL_INFO("Starting continuous flow reading at rate {}/{}", rate.numerator, rate.denominator);

            auto const windowSize = gstPipeline._config.nbSamplesPerBatch; // samples per read
            mxlWrappedMultiBufferSlice payload;

            auto index = mxlGetCurrentIndex(&rate);

            while (!g_exit_requested)
            {
                auto ret = mxlFlowReaderGetSamples(_reader, index - playbackOffset, windowSize, &payload);
                if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                {
                    // We are too early somehow, keep trying the same index
                    mxlFlowReaderGetInfo(_reader, &_flowInfo);
                    MXL_WARN("Failed to get samples at index {}: TOO EARLY. Last published {}", index, _flowInfo.continuous.headIndex);
                    continue;
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
                {
                    MXL_WARN("Failed to get samples at index {}: TOO LATE", index);
                    // We are too late, that's too bad. Time travel!
                    index = mxlGetCurrentIndex(&rate);
                    continue;
                }
                else if (ret != MXL_STATUS_OK)
                {
                    MXL_ERROR("Unexpected error when reading the grain {} with status '{}'. Exiting.", index, static_cast<int>(ret));
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

    public:

    private:
        mxlFlowInfo _flowInfo;
        mxlInstance _instance;
        mxlFlowReader _reader;
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

        std::optional<std::uint64_t> readTimeoutNs;
        auto readTimeoutOpt = app.add_option("-t,--timeout", readTimeoutNs, "The read timeout in ns, frame interval + 1 ms used if not specified");
        readTimeoutOpt->required(false);
        readTimeoutOpt->default_val(std::nullopt);

        std::vector<std::size_t> listenChannels;
        auto listenChanOpt = app.add_option("-l, --listen-channels", listenChannels, "Audio channels to listen");
        listenChanOpt->default_val(std::vector<std::size_t>{0, 1});

        int64_t sampleOffset;
        auto sampleOffsetOpt = app.add_option("--audio-offset", sampleOffset, "Audio offset in samples. Positive value means you are adding a delay");
        sampleOffsetOpt->default_val(48);

        int64_t grainOffset;
        auto grainOffsetOpt = app.add_option("--video-offset", grainOffset, "Video offset in grains. Positive value means you are adding a delay");
        grainOffsetOpt->default_val(0);

        uint64_t samplesPerBatch;
        auto samplesPerBatchOpt = app.add_option("-s, --samples-per-batch",
            samplesPerBatch,
            "Number of audio samples per batch. Should be the same or bigger than the videotestsrc setting.");
        samplesPerBatchOpt->default_val(48);

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

                    GstreamerVideoPipelineConfig videoConfig{
                        .frame_width = static_cast<std::uint64_t>(parser.get<double>("frame_width")),
                        .frame_height = static_cast<std::uint64_t>(parser.get<double>("frame_height")),
                        .frame_rate = parser.getGrainRate(),
                    };

                    auto pipeline = GstreamerVideoPipeline(videoConfig);
                    reader.run(pipeline, grainOffset);

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
                        .nbSamplesPerBatch = samplesPerBatch,
                        .spkrEnabled = listenChannels,
                    };

                    auto pipeline = GstreamerAudioPipeline(audioConfig);
                    reader.run(pipeline, sampleOffset);

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
