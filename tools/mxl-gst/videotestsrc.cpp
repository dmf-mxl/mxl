// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstdint>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/FlowParser.hpp"
#include "mxl-internal/Logging.hpp"

std::sig_atomic_t volatile g_exit_requested = 0;

void signal_handler(int)
{
    g_exit_requested = 1;
}

struct VideoPipelineConfig
{
    uint64_t frame_width{1920};
    uint64_t frame_height{1080};
    mxlRational frame_rate{30, 1};
    uint64_t pattern{0};
    std::string textoverlay{"EBU DMF MXL"};

    [[nodiscard]]
    std::string display() const noexcept
    {
        return fmt::format("frame_width={} frame_height={} frame_rate={}/{} pattern={} textoverlay={}",
            frame_width,
            frame_height,
            frame_rate.numerator,
            frame_rate.denominator,
            pattern,
            textoverlay);
    }
};

struct AudioPipelineConfig
{
    mxlRational rate{48000, 1};
    std::size_t channelCount{2};
    double freq{440.0};
    uint64_t samplesPerBatch{1024};
    uint64_t wave{0};
    int64_t offset{0};

    [[nodiscard]]
    std::string display() const noexcept
    {
        return fmt::format("rate={} channelCount={} freq={} samplesPerBatch={} wave={}", rate.numerator, channelCount, freq, samplesPerBatch, wave);
    }
};

static std::unordered_map<std::string, uint64_t> const pattern_map = {
    {"smpte",             0 }, // SMPTE 100% color bars
    {"snow",              1 }, // Random noise
    {"black",             2 }, // Solid black
    {"white",             3 }, // Solid white
    {"red",               4 }, // Solid red
    {"green",             5 }, // Solid green
    {"blue",              6 }, // Solid blue
    {"checkers-1",        7 }, // Checkers 1
    {"checkers-2",        8 }, // Checkers 2
    {"checkers-4",        9 }, // Checkers 4
    {"checkers-8",        10}, // Checkers 8
    {"circular",          11}, // Circular
    {"blink",             12}, // Blink
    {"smpte75",           13}, // SMPTE 75% color bars
    {"zone-plate",        14}, // Zone plate
    {"gamut",             15}, // Gamut checkers
    {"chroma-zone-plate", 16}, // Chroma zone plate
    {"solid-color",       17}, // Solid color
    {"ball",              18}, // Moving ball
    {"smpte100",          19}, // SMPTE 100% color bars
    {"bar",               20}, // Bar
    {"pinwheel",          21}, // Pinwheel
    {"spokes",            22}, // Spokes
    {"gradient",          23}, // Gradient
    {"colors",            24}  // Colors
};

static std::unordered_map<std::string, uint64_t> const wave_map = {
    {"sine",           0 }, // Sine
    {"square",         1 }, // Square
    {"saw",            2 }, // Saw
    {"triangle",       3 }, // Triangle
    {"silence",        4 }, // Silence
    {"white-noise",    5 }, // White uniform noise
    {"pink-noise",     6 }, // Pink noise
    {"sine-table",     7 }, // Sine Table
    {"ticks",          8 }, // Periodic Ticks
    {"gaussian-noise", 9 }, // White Gaussian noise
    {"red-noise",      10}, // Red (brownian) noise
    {"blue-noise",     11}, // Blue noise
    {"violet-noise",   12}, // Violet noise
};

class GstreamerPipeline
{
public:
    virtual void start() = 0;
    virtual GstSample* pullSample() = 0;
};

class VideoPipeline final : public GstreamerPipeline
{
public:
    VideoPipeline(VideoPipelineConfig const& config)
    {
        MXL_INFO("Creating Videopipeline with config: {}", config.display());

        auto pipelineDesc = fmt::format(
            "videotestsrc is-live=true pattern={} ! "
            "video/x-raw,format=v210,width={},height={},framerate={}/{} ! "
            "textoverlay text=\"{}\" font-desc=\"Sans, 36\" ! "
            "clockoverlay ! "
            "videoconvert ! "
            "videoscale ! "
            "appsink name=appsink ",
            config.pattern,
            config.frame_width,
            config.frame_height,
            config.frame_rate.numerator,
            config.frame_rate.denominator,
            config.textoverlay);
        MXL_INFO("Generating following Video gsteamer pipeline -> {}", pipelineDesc);

        GError* error = nullptr;
        _pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
        if (!_pipeline || error)
        {
            MXL_ERROR("Failed to create pipeline: {}", error->message);
            g_error_free(error);
            throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
        }

        _appsink = gst_bin_get_by_name(GST_BIN(_pipeline), "appsink");
        if (_appsink == nullptr)
        {
            throw std::runtime_error("Gstreamer: 'appsink' could not be found in the pipeline.");
        }

        // Configure appsink
        g_object_set(G_OBJECT(_appsink),
            "caps",
            gst_caps_new_simple("video/x-raw",
                "format",
                G_TYPE_STRING,
                "v210",
                "width",
                G_TYPE_INT,
                config.frame_width,
                "height",
                G_TYPE_INT,
                config.frame_height,
                "framerate",
                GST_TYPE_FRACTION,
                config.frame_rate.numerator,
                config.frame_rate.denominator,
                nullptr),
            "max-buffers",
            16,
            nullptr);

        // Create the empty pipeline
    }

    ~VideoPipeline()
    {
        if (_pipeline)
        {
            gst_element_set_state(_pipeline, GST_STATE_NULL);
            gst_object_unref(_pipeline);
        }

        if (_appsink)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_appsink))
            {
                gst_object_unref(_appsink);
            }
        }
    }

    void start() final
    {
        gst_element_set_state(_pipeline, GST_STATE_PLAYING);
    }

    GstSample* pullSample() final
    {
        return gst_app_sink_pull_sample(GST_APP_SINK(_appsink));
    }

private:
    GstElement* _appsink{nullptr};
    GstElement* _pipeline{nullptr};
};

class AudioPipeline final : public GstreamerPipeline
{
public:
    AudioPipeline(AudioPipelineConfig const& config)
        : _config(config)
    {
        MXL_INFO("Creating Audiopipeline with config: {}", config.display());

        std::string pipelineDesc;
        for (size_t chan = 0; chan < config.channelCount; ++chan)
        {
            auto freq = (chan + 1) * 100;
            pipelineDesc += fmt::format(
                "audiotestsrc freq={} samplesperbuffer={} ! audio/x-raw,channels=1 ! queue ! m.sink_{} ", freq, config.samplesPerBatch, chan);
        }
        pipelineDesc += fmt::format("interleave name=m ! \
                     audioconvert ! \
                     queue !\
                     audio/x-raw,format=F32LE,rate=48000,channels={},layout=non-interleaved ! \
                     queue ! \
                     appsink name=appsink",
            config.channelCount);

        MXL_INFO("Generating gstremer pipeline ->\n {}", pipelineDesc);

        GError* error = nullptr;
        _pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
        if (!_pipeline || error)
        {
            MXL_ERROR("Failed to create pipeline: {}", error->message);
            g_error_free(error);
            throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
        }

        _appsink = gst_bin_get_by_name(GST_BIN(_pipeline), "appsink");
        if (_appsink == nullptr)
        {
            throw std::runtime_error("Gstreamer: 'appsink' could not be found in the pipeline.");
        }
    }

    ~AudioPipeline()
    {
        if (_pipeline)
        {
            gst_element_set_state(_pipeline, GST_STATE_NULL);
            gst_object_unref(_pipeline);
        }

        if (_appsink)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_appsink))
            {
                gst_object_unref(_appsink);
            }
        }
    }

    void start() final
    {
        gst_element_set_state(_pipeline, GST_STATE_PLAYING);
    }

    GstSample* pullSample() final
    {
        return gst_app_sink_pull_sample(GST_APP_SINK(_appsink));
    }

private:
    AudioPipelineConfig _config;

    GstElement* _appsink{nullptr};
    GstElement* _pipeline{nullptr};
};

class MxlWriter
{
public:
    MxlWriter(std::string const& domain, std::string const& flow_descriptor)
    {
        _instance = mxlCreateInstance(domain.c_str(), "");
        if (_instance == nullptr)
        {
            throw std::runtime_error("Failed to create MXL instance");
        }

        mxlStatus ret;

        // Create the flow
        ret = mxlCreateFlow(_instance, flow_descriptor.c_str(), nullptr, &_flowInfo);
        if (ret != MXL_STATUS_OK)
        {
            throw std::runtime_error("Failed to create flow with status '" + std::to_string(static_cast<int>(ret)) + "'");
        }
        _flowOpened = true;

        auto flowID = uuids::to_string(uuids::uuid(_flowInfo.common.id));

        // Create the flow writer
        if (mxlCreateFlowWriter(_instance, flowID.c_str(), "", &_writer) != MXL_STATUS_OK)
        {
            throw std::runtime_error("Failed to create flow writer");
        }
    }

    ~MxlWriter()
    {
        if (_writer != nullptr)
        {
            mxlReleaseFlowWriter(_instance, _writer);
        }

        if (_flowOpened)
        {
            auto flowID = uuids::to_string(uuids::uuid(_flowInfo.common.id));
            MXL_INFO("Destroying flow -> {}", flowID);
            mxlDestroyFlow(_instance, flowID.c_str());
        }

        if (_instance != nullptr)
        {
            mxlDestroyInstance(_instance);
        }
    }

    int run(GstreamerPipeline& gst_pipeline, std::int64_t playbackOffset)
    {
        gst_pipeline.start();

        if (mxlIsDiscreteDataFormat(_flowInfo.common.format))
        {
            return runDiscreteFlow(dynamic_cast<VideoPipeline&>(gst_pipeline), _flowInfo.discrete.grainRate, playbackOffset);
        }
        else
        {
            return runContinuousFlow(dynamic_cast<AudioPipeline&>(gst_pipeline), _flowInfo.continuous.sampleRate, playbackOffset);
        }
    }

private:
    int runDiscreteFlow(VideoPipeline& gst_pipeline, mxlRational const& frame_rate, std::int64_t playbackOffset)
    {
        GstSample* gst_sample;
        GstBuffer* gst_buffer;
        uint64_t grain_index;

        while (!g_exit_requested)
        {
            gst_sample = gst_pipeline.pullSample();
            if (gst_sample)
            {
                grain_index = mxlGetCurrentIndex(&frame_rate);

                gst_buffer = gst_sample_get_buffer(gst_sample);
                if (gst_buffer)
                {
                    gst_buffer_ref(gst_buffer);

                    GstMapInfo map_info;
                    if (gst_buffer_map(gst_buffer, &map_info, GST_MAP_READ))
                    {
                        /// Open the grain.
                        mxlGrainInfo gInfo;
                        uint8_t* mxl_buffer = nullptr;

                        /// Open the grain for writing.
                        if (mxlFlowWriterOpenGrain(_writer, grain_index - playbackOffset, &gInfo, &mxl_buffer) != MXL_STATUS_OK)
                        {
                            MXL_ERROR("Failed to open grain at index '{}'", grain_index);
                            break;
                        }

                        ::memcpy(mxl_buffer, map_info.data, gInfo.grainSize);

                        gInfo.validSlices = gInfo.totalSlices;
                        if (mxlFlowWriterCommitGrain(_writer, &gInfo) != MXL_STATUS_OK)
                        {
                            MXL_ERROR("Failed to open grain at index '{}'", grain_index);
                            break;
                        }

                        gst_buffer_unmap(gst_buffer, &map_info);
                    }
                    gst_buffer_unref(gst_buffer);
                }

                gst_sample_unref(gst_sample);

                auto ns = mxlGetNsUntilIndex(++grain_index, &frame_rate);
                mxlSleepForNs(ns);
            }
        }

        return 0;
    }

    int runContinuousFlow(AudioPipeline& gst_pipeline, mxlRational const& rate, std::int64_t playbackOffset)
    {
        GstSample* gst_sample;
        GstBuffer* gst_buffer;
        uint64_t headIndex = 0;

        bool first{true};

        while (!g_exit_requested)
        {
            gst_sample = gst_pipeline.pullSample();
            if (gst_sample)
            {
                gst_buffer = gst_sample_get_buffer(gst_sample);
                if (gst_buffer)
                {
                    gst_buffer_ref(gst_buffer);

                    GstMapInfo map_info;
                    if (gst_buffer_map(gst_buffer, &map_info, GST_MAP_READ))
                    {
                        if (first)
                        {
                            headIndex = mxlGetCurrentIndex(&rate);
                            first = false;
                            MXL_INFO("Audio Flow starting at sample \"{}\" with rate {}/{}", headIndex, rate.numerator, rate.denominator);
                        }

                        auto nbSamplesPerChan = map_info.size / (4 * _flowInfo.continuous.channelCount);

                        mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
                        if (mxlFlowWriterOpenSamples(_writer, headIndex - playbackOffset, nbSamplesPerChan, &payloadBuffersSlices))
                        {
                            MXL_ERROR("Failed to open grain at index '{}'", headIndex);
                            break;
                        }

                        std::uintptr_t offset = 0;
                        for (uint64_t chan = 0; chan < payloadBuffersSlices.count; ++chan)
                        {
                            for (auto& fragment : payloadBuffersSlices.base.fragments)
                            {
                                if (fragment.size != 0)
                                {
                                    auto dst = reinterpret_cast<std::uint8_t*>(fragment.pointer) + (chan * payloadBuffersSlices.stride);
                                    auto src = map_info.data + offset;
                                    ::memcpy(dst, src, fragment.size);
                                    offset += fragment.size;
                                }
                            }
                        }
                        if (mxlFlowWriterCommitSamples(_writer) != MXL_STATUS_OK)
                        {
                            MXL_ERROR("Failed to open grain at index '{}'", headIndex);
                            break;
                        }

                        headIndex += nbSamplesPerChan;

                        gst_buffer_unmap(gst_buffer, &map_info);
                    }
                    gst_buffer_unref(gst_buffer);
                }

                gst_sample_unref(gst_sample);

                if (!first)
                {
                    auto ns = mxlGetNsUntilIndex(headIndex, &rate);
                    mxlSleepForNs(ns);
                }
            }
        }
        return 0;
    }

private:
    mxlInstance _instance;
    mxlFlowWriter _writer;
    mxlFlowInfo _flowInfo;
    bool _flowOpened;
};

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-gst-videotestsrc");

    std::string videoFlowConfigFile;
    auto videoFlowConfigFileOpt = app.add_option(
        "-v, --video-config-file", videoFlowConfigFile, "The json file which contains the Video NMOS Flow configuration");
    videoFlowConfigFileOpt->default_val("");

    std::string audioFlowConfigFile;
    auto audioFlowConfigFileOpt = app.add_option(
        "-a, --audio-config-file", audioFlowConfigFile, "The json file which contains the Audio NMOS Flow configuration");
    audioFlowConfigFileOpt->default_val("");

    uint64_t samplesPerBatch;
    auto samplesPerBatchOpt = app.add_option("-s, --samples-per-batch", samplesPerBatch, "Number of audio samples per batch");
    samplesPerBatchOpt->default_val(48);

    int64_t audioOffset;
    auto audioOffsetOpt = app.add_option(
        "--audio-offset", audioOffset, "Audio sample offset in number of samples. Positive value means you are adding a delay (commit in the past).");
    audioOffsetOpt->default_val(0);

    int64_t videoOffset;
    auto videoOffsetOpt = app.add_option(
        "--video-offset", videoOffset, "Video grain offset in number of grains. Positive value means you are adding a delay (commit in the past).");
    videoOffsetOpt->default_val(0);

    std::string domain;
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    std::string pattern;
    auto patternOpt = app.add_option("-p, --pattern",
        pattern,
        "Test pattern to use. For available options see "
        "https://gstreamer.freedesktop.org/documentation/videotestsrc/index.html?gi-language=c#GstVideoTestSrcPattern");
    patternOpt->default_val("smpte");

    std::string textOverlay;
    auto textOverlayOpt = app.add_option("-t,--overlay-text", textOverlay, "Change the text overlay of the test source");
    textOverlayOpt->default_val("EBU DMF MXL");

    CLI11_PARSE(app, argc, argv);

    gst_init(nullptr, nullptr);

    std::vector<std::thread> threads;

    if (!videoFlowConfigFile.empty())
    {
        threads.emplace_back(
            [&]()
            {
                std::ifstream file(videoFlowConfigFile, std::ios::in | std::ios::binary);
                if (!file)
                {
                    MXL_ERROR("Failed to open file: '{}'", videoFlowConfigFile);
                    return EXIT_FAILURE;
                }
                std::string flow_descriptor{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                mxl::lib::FlowParser descriptor_parser{flow_descriptor};

                auto frame_rate = descriptor_parser.getGrainRate();

                VideoPipelineConfig gst_config{
                    .frame_width = static_cast<uint64_t>(descriptor_parser.get<double>("frame_width")),
                    .frame_height = static_cast<uint64_t>(descriptor_parser.get<double>("frame_height")),
                    .frame_rate = frame_rate,
                    .pattern = pattern_map.at(pattern),
                    .textoverlay = textOverlay,
                };

                VideoPipeline gst_pipeline(gst_config);

                auto mxlWriter = MxlWriter(domain, flow_descriptor);
                mxlWriter.run(gst_pipeline, videoOffset);

                MXL_INFO("Video pipeline finished");
                return 0;
            });
    }

    if (!audioFlowConfigFile.empty())
    {
        threads.emplace_back(
            [&]()
            {
                std::ifstream file(audioFlowConfigFile, std::ios::in | std::ios::binary);
                if (!file)
                {
                    MXL_ERROR("Failed to open file: '{}'", audioFlowConfigFile);
                    return EXIT_FAILURE;
                }
                std::string flow_descriptor{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                mxl::lib::FlowParser descriptor_parser{flow_descriptor};

                AudioPipelineConfig gst_config{
                    .rate = descriptor_parser.getGrainRate(),
                    .channelCount = descriptor_parser.getChannelCount(),
                    .freq = 440.0,
                    .samplesPerBatch = samplesPerBatch,
                    .wave = wave_map.at("sine"),
                };

                AudioPipeline gst_pipeline(gst_config);

                auto mxlWriter = MxlWriter(domain, flow_descriptor);
                mxlWriter.run(gst_pipeline, audioOffset);

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
