// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <csignal>
#include <cstdint>
#include <optional>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/gstsystemclock.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/FlowOptionsParser.hpp"
#include "mxl-internal/FlowParser.hpp"
#include "mxl-internal/Logging.hpp"

namespace
{
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
        std::uint32_t sliceSize;

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
        std::uint32_t samplesPerBatch;
        uint64_t wave{0};
        int64_t offset{0};

        [[nodiscard]]
        std::string display() const noexcept
        {
            return fmt::format("rate={} channelCount={}  samplesPerBatch={} wave={}", rate.numerator, channelCount, samplesPerBatch, wave);
        }
    };

    std::unordered_map<std::string, uint64_t> const pattern_map = {
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

    std::unordered_map<std::string, uint64_t> const wave_map = {
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
        virtual std::optional<std::pair<GstSample*, GstBuffer*>> pull(std::uint64_t timeout) = 0;
    };

    class VideoPipeline final : public GstreamerPipeline
    {
    public:
        VideoPipeline(VideoPipelineConfig const& config)
            : _config(config)
        {
            MXL_INFO("Creating Videopipeline with config: {}", config.display());

            auto pipelineDesc = fmt::format(
                "videotestsrc name=videotestsrc is-live=true do-timestamp=true pattern={} ! "
                "video/x-raw,format=v210,width={},height={},framerate={}/{} ! "
                "textoverlay text=\"{}\" font-desc=\"Sans, 36\" ! "
                "clockoverlay ! "
                "videoconvert ! "
                "videoscale ! "
                "queue ! "
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

                nullptr);

            if (auto clock = gst_pipeline_get_clock(GST_PIPELINE(_pipeline)); clock)
            {
                g_object_set(G_OBJECT(clock), "clock-type", GST_CLOCK_TYPE_TAI, nullptr);
                gst_object_unref(clock);
            }
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
            // Here we want to align the gstreamer pipeline as close as possible to an epoch frame. To achieve this, we set the base time to the next
            // frame timestamp.
            auto baseTime = mxlIndexToTimestamp(&_config.frame_rate, mxlGetCurrentIndex(&_config.frame_rate) + 1);
            gst_element_set_base_time(_pipeline, baseTime);
            MXL_INFO("VideoPipeline: Set gst base time to {} ns", baseTime);
            gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            _gstBaseTime = gst_element_get_base_time(_pipeline);
            MXL_INFO("VideoPipeline: Gst base time: {} ns", _gstBaseTime);
        }

        std::optional<std::pair<GstSample*, GstBuffer*>> pull(std::uint64_t timeout) final
        {
            if (auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(_appsink), timeout); sample)
            {
                auto buffer = gst_sample_get_buffer(sample);
                if (buffer)
                {
                    gst_buffer_ref(buffer);
                    auto bufferTs = GST_BUFFER_PTS(buffer);

                    if (!_internalOffset)
                    {
                        _internalOffset = static_cast<std::int64_t>(__int128_t{mxlGetTime()} - __int128_t{bufferTs + _gstBaseTime});
                        MXL_INFO("VideoPipeline: Set internal offset to {} ns", *_internalOffset);
                    }

                    GST_BUFFER_PTS(buffer) = bufferTs + _gstBaseTime + *_internalOffset; // Change the PTS to TAI time and include preroll delay

                    return std::make_pair(sample, buffer);
                }
                else
                {
                    gst_sample_unref(sample);
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        VideoPipelineConfig _config;

    private:
        std::optional<std::int64_t> _internalOffset{std::nullopt};
        std::uint64_t _gstBaseTime{0};

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

            auto samplesPerBatch = config.samplesPerBatch;

            std::string pipelineDesc;
            for (size_t chan = 0; chan < config.channelCount; ++chan)
            {
                auto freq = (chan + 1) * 100;
                pipelineDesc += fmt::format(
                    "audiotestsrc is-live=true do-timestamp=true freq={} samplesperbuffer={} ! audio/x-raw,channels=1 ! queue ! m.sink_{} ",
                    freq,
                    samplesPerBatch,
                    chan);
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

            if (auto clock = gst_pipeline_get_clock(GST_PIPELINE(_pipeline)); clock)
            {
                g_object_set(G_OBJECT(clock), "clock-type", GST_CLOCK_TYPE_TAI, nullptr);
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
            // Here we want to align the gstreamer pipeline as close as possible to an epoch sample. To achieve this, we set the base time to the next
            // sample timestamp.
            auto baseTime = mxlIndexToTimestamp(&_config.rate, mxlGetCurrentIndex(&_config.rate) + 1);
            gst_element_set_base_time(_pipeline, baseTime);
            MXL_INFO("AudioPipeline: Set gst base time to {} ns", baseTime);
            gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            _gstBaseTime = gst_element_get_base_time(_pipeline);
            MXL_INFO("AudioPipeline: Gst base time: {} ns", _gstBaseTime);
        }

        std::optional<std::pair<GstSample*, GstBuffer*>> pull(std::uint64_t timeout) final
        {
            if (auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(_appsink), timeout); sample)
            {
                auto buffer = gst_sample_get_buffer(sample);
                if (buffer)
                {
                    gst_buffer_ref(buffer);
                    auto bufferTs = GST_BUFFER_PTS(buffer);

                    if (!_internalOffset)
                    {
                        _internalOffset = static_cast<std::int64_t>(__int128_t{mxlGetTime()} - __int128_t{bufferTs + _gstBaseTime});
                        MXL_INFO("AudioPipeline: Set internal offset to {} ns", *_internalOffset);
                    }

                    GST_BUFFER_PTS(buffer) = bufferTs + _gstBaseTime + *_internalOffset; // Change the PTS to TAI time and include preroll delay

                    return std::make_pair(sample, buffer);
                }
                else
                {
                    gst_sample_unref(sample);
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        AudioPipelineConfig _config;

    private:
        std::optional<std::int64_t> _internalOffset{std::nullopt};
        std::uint64_t _gstBaseTime{0};

        GstElement* _appsink{nullptr};
        GstElement* _pipeline{nullptr};
    };

    class MxlWriter
    {
    public:
        MxlWriter(std::string const& domain, std::string const& flow_descriptor, std::string const& flowOptions)
        {
            _instance = mxlCreateInstance(domain.c_str(), "");
            if (_instance == nullptr)
            {
                throw std::runtime_error("Failed to create MXL instance");
            }

            mxlStatus ret;

            // Create the flow
            ret = mxlCreateFlow(_instance, flow_descriptor.c_str(), flowOptions.c_str(), &_configInfo);
            if (ret != MXL_STATUS_OK)
            {
                throw std::runtime_error("Failed to create flow with status '" + std::to_string(static_cast<int>(ret)) + "'");
            }
            _flowOpened = true;

            auto flowID = uuids::to_string(uuids::uuid(_configInfo.common.id));

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
                auto flowID = uuids::to_string(uuids::uuid(_configInfo.common.id));
                MXL_INFO("Destroying flow -> {}", flowID);
                mxlDestroyFlow(_instance, flowID.c_str());
            }

            if (_instance != nullptr)
            {
                mxlDestroyInstance(_instance);
            }
        }

        std::uint32_t maxCommitBatchSizeHint() const
        {
            return _configInfo.common.maxCommitBatchSizeHint;
        }

        int run(GstreamerPipeline& gst_pipeline, std::int64_t offset)
        {
            gst_pipeline.start();

            if (mxlIsDiscreteDataFormat(_configInfo.common.format))
            {
                return runDiscreteFlow(dynamic_cast<VideoPipeline&>(gst_pipeline), offset);
            }
            else
            {
                return runContinuousFlow(dynamic_cast<AudioPipeline&>(gst_pipeline), offset);
            }
        }

    private:
        int runDiscreteFlow(VideoPipeline& gst_pipeline, std::int64_t offset)
        {
            std::optional<std::uint64_t> grainIndex;

            auto slicesPerBatch = _configInfo.common.maxCommitBatchSizeHint;
            while (!g_exit_requested)
            {
                auto timeout = grainIndex ? mxlGetNsUntilIndex(*grainIndex + 1, &gst_pipeline._config.frame_rate)
                                          : std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100))
                                                .count(); // arbitrary high timeout for the first grain
                if (auto ok = gst_pipeline.pull(timeout); ok)
                {
                    auto [gstSample, gstBuffer] = *ok;

                    auto bufferTs = GST_BUFFER_PTS(gstBuffer); // PTS was already converted to TAI time in the pull()
                    auto gstGrainIndex = mxlTimestampToIndex(&gst_pipeline._config.frame_rate, bufferTs);

                    // First buffer, set the initial grain index
                    if (!grainIndex)
                    {
                        grainIndex = gstGrainIndex;
                        MXL_INFO("DiscreteFlow: Set initial grain index to {} (bufferTs={} ns)", *grainIndex, bufferTs);
                    }

                    // verify that we didn't miss any grains
                    if (gstGrainIndex < *grainIndex) // gstreamer index is smaller than we expected. time went backward??
                    {
                        MXL_ERROR(
                            "Unexpected grain index from gstreamer PTS {} expected grain index {}. Time went backward??", gstGrainIndex, *grainIndex);
                    }
                    else if (gstGrainIndex > *grainIndex) // gstreamer index is bigger than we expected
                    {
                        MXL_WARN(
                            "DiscreteFlow: Skipped grain(s). Expected grain index {}, got grain index {} (bufferTs={} ns). Generating {} skipped grains",
                            *grainIndex,
                            gstGrainIndex,
                            bufferTs,
                            gstGrainIndex - *grainIndex);

                        // Generate the skipped grains as invalid grains
                        for (; *grainIndex < gstGrainIndex; ++*grainIndex)
                        {
                            mxlGrainInfo gInfo;
                            uint8_t* mxlBuffer = nullptr;

                            auto actualGrainIndex = *grainIndex - offset;
                            /// Open the grain for writing.
                            if (mxlFlowWriterOpenGrain(_writer, actualGrainIndex, &gInfo, &mxlBuffer) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to open grain at index '{}'", actualGrainIndex);
                                break;
                            }

                            gInfo.flags = MXL_GRAIN_FLAG_INVALID;
                            if (mxlFlowWriterCommitGrain(_writer, &gInfo) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to commit invalid grain at index '{}'", actualGrainIndex);
                                break;
                            }
                        }
                    }
                    else // gsteamer index matches expected index
                    {
                        auto actualGrainIndex = *grainIndex - offset;

                        auto nbBatches = (gst_pipeline._config.frame_height + (slicesPerBatch - 1)) /
                                         slicesPerBatch; // Calculate how many batches are required to commit the full grain.

                        GstMapInfo map_info;
                        if (gst_buffer_map(gstBuffer, &map_info, GST_MAP_READ))
                        {
                            /// Open the grain.
                            mxlGrainInfo gInfo;
                            uint8_t* mxlBuffer = nullptr;

                            /// Open the grain for writing.
                            if (mxlFlowWriterOpenGrain(_writer, actualGrainIndex, &gInfo, &mxlBuffer) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to open grain at index '{}'", actualGrainIndex);
                                break;
                            }

                            gInfo.validSlices = 0;

                            auto sliceSize = gst_pipeline._config.sliceSize;

                            std::uint64_t sleepTimeBetweenBatches = 0;
                            if (nbBatches > 1)
                            {
                                // spread the batches across half a frame period.
                                sleepTimeBetweenBatches = 1'000'000'000ULL * gst_pipeline._config.frame_rate.denominator /
                                                          gst_pipeline._config.frame_rate.numerator / (nbBatches * 2);

                                // Any sleep under 50us will most likely not work. If it's the case, just skip the sleeps.
                                if (sleepTimeBetweenBatches < 50'000)
                                {
                                    sleepTimeBetweenBatches = 0;
                                }
                            }

                            for (size_t i = 0; i < nbBatches; i++)
                            {
                                auto nbSlices = std::min<std::uint16_t>(slicesPerBatch, gInfo.totalSlices - gInfo.validSlices);

                                ::memcpy(
                                    mxlBuffer + (sliceSize * slicesPerBatch * i), map_info.data + (sliceSize * slicesPerBatch * i), nbSlices * sliceSize);
                                gInfo.validSlices += nbSlices;

                                if (mxlFlowWriterCommitGrain(_writer, &gInfo) != MXL_STATUS_OK)
                                {
                                    MXL_ERROR("Failed to commit grain at index '{}'", actualGrainIndex);
                                    break;
                                }

                                std::this_thread::sleep_for(std::chrono::nanoseconds(sleepTimeBetweenBatches));
                            }

                            gst_buffer_unmap(gstBuffer, &map_info);
                        }
                        else
                        {
                            MXL_WARN("Failed to map gst buffer for grain index '{}'", actualGrainIndex);
                        }
                    }

                    gst_buffer_unref(gstBuffer);
                    gst_sample_unref(gstSample);

                    grainIndex = *grainIndex + 1;
                    mxlSleepForNs(mxlGetNsUntilIndex(*grainIndex, &gst_pipeline._config.frame_rate));
                }
            }

            return 0;
        }

        int runContinuousFlow(AudioPipeline& gst_pipeline, std::int64_t offset)

        {
            std::optional<std::uint64_t> sampleIndex;

            auto samplesPerBatch = gst_pipeline._config.samplesPerBatch;

            while (!g_exit_requested)
            {
                auto timeout = sampleIndex ? mxlGetNsUntilIndex(*sampleIndex + samplesPerBatch, &gst_pipeline._config.rate)
                                           : std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(100))
                                                 .count(); // arbitrary high timeout for the first sample

                if (auto ok = gst_pipeline.pull(timeout); ok)
                {
                    auto [gstSample, gstBuffer] = *ok;

                    auto bufferTs = GST_BUFFER_PTS(gstBuffer); // PTS was already converted to TAI time in the pull()
                    auto gstSampleIndex = mxlTimestampToIndex(&gst_pipeline._config.rate, bufferTs);

                    // First buffer, set the initial sample index
                    if (!sampleIndex)
                    {
                        sampleIndex = gstSampleIndex;
                        MXL_INFO("ContinuousFlow: Set initial sample index to {} (bufferTs={} ns)", *sampleIndex, bufferTs);
                    }
                    // Verify that we didnt miss any samples
                    if (gstSampleIndex < *sampleIndex) // gstreamer index is smaller than we expected. time went backward??
                    {
                        MXL_ERROR(
                            "Unexpected sample index from gstreamer PTS {} expected sample index {}. Time went backward??", gstSampleIndex, *sampleIndex);
                    }
                    else if (gstSampleIndex > *sampleIndex) // gstreamer index is bigger than we expected
                    {
                        MXL_WARN("ContinuousFlow: Skipped sample(s). Expected sample index {}, got sample index {} (bufferTs={} ns). Generating {} "
                                 "silenced samples",
                            *sampleIndex,
                            gstSampleIndex,
                            bufferTs,
                            gstSampleIndex - *sampleIndex);

                        // Generate the skipped samples as silenced samples.
                        // ** A production application should apply a fade when inserting silence to avoid audio artefacts
                        auto nbSamples = gstSampleIndex - *sampleIndex;
                        auto actualSampleIndex = gstSampleIndex - offset;

                        mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
                        if (mxlFlowWriterOpenSamples(_writer, actualSampleIndex, nbSamples, &payloadBuffersSlices))
                        {
                            MXL_ERROR("Failed to open samples at index '{}'", actualSampleIndex);
                            break;
                        }

                        for (uint64_t chan = 0; chan < payloadBuffersSlices.count; ++chan)
                        {
                            for (auto& fragment : payloadBuffersSlices.base.fragments)
                            {
                                if (fragment.size != 0)
                                {
                                    auto dst = reinterpret_cast<std::uint8_t*>(fragment.pointer) + (chan * payloadBuffersSlices.stride);
                                    ::memset(dst, 0, fragment.size); // fill with silence
                                }
                            }
                        }
                    }
                    else // gsteamer index matches expected index
                    {
                        auto actualSampleIndex = *sampleIndex - offset;

                        GstMapInfo map_info;
                        if (gst_buffer_map(gstBuffer, &map_info, GST_MAP_READ))
                        {
                            auto nbSamplesPerChan = map_info.size / (4 * _configInfo.continuous.channelCount);

                            mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
                            if (mxlFlowWriterOpenSamples(_writer, actualSampleIndex, nbSamplesPerChan, &payloadBuffersSlices))
                            {
                                MXL_ERROR("Failed to open samples at index '{}'", actualSampleIndex);
                                break;
                            }

                            std::uintptr_t addrOffset = 0;
                            for (uint64_t chan = 0; chan < payloadBuffersSlices.count; ++chan)
                            {
                                for (auto& fragment : payloadBuffersSlices.base.fragments)
                                {
                                    if (fragment.size != 0)
                                    {
                                        auto dst = reinterpret_cast<std::uint8_t*>(fragment.pointer) + (chan * payloadBuffersSlices.stride);
                                        auto src = map_info.data + addrOffset;
                                        ::memcpy(dst, src, fragment.size);
                                        addrOffset += fragment.size;
                                    }
                                }
                            }

                            if (mxlFlowWriterCommitSamples(_writer) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to open samples at index '{}'", actualSampleIndex);
                                break;
                            }
                        }

                        gst_buffer_unmap(gstBuffer, &map_info);
                    }

                    gst_buffer_unref(gstBuffer);
                    gst_sample_unref(gstSample);

                    sampleIndex = *sampleIndex + samplesPerBatch;
                    mxlSleepForNs(mxlGetNsUntilIndex(*sampleIndex, &gst_pipeline._config.rate));
                }
            }

            return 0;
        }

    private:
        mxlInstance _instance;

        mxlFlowWriter _writer;
        mxlFlowConfigInfo _configInfo;
        bool _flowOpened;
    };
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-gst-videotestsrc");

    std::string videoFlowConfigFile;
    auto videoFlowConfigFileOpt = app.add_option(
        "-v, --video-config-file", videoFlowConfigFile, "The json file which contains the Video NMOS Flow configuration.");
    videoFlowConfigFileOpt->default_val("");

    std::string videoFlowOptionFile;
    auto videoFlowOptionFileOpt = app.add_option("--video-options-file", videoFlowOptionFile, "The json file which contains the Video Flow options.");
    videoFlowOptionFileOpt->default_val("");

    std::string audioFlowConfigFile;
    auto audioFlowConfigFileOpt = app.add_option(
        "-a, --audio-config-file", audioFlowConfigFile, "The json file which contains the Audio NMOS Flow configuration");
    audioFlowConfigFileOpt->default_val("");

    std::string audioFlowOptionFile;
    auto audioFlowOptionFileOpt = app.add_option("--audio-options-file", audioFlowOptionFile, "The json file which contains the Audio Flow options.");
    audioFlowOptionFileOpt->default_val("");

    int64_t audioOffset;
    auto audioOffsetOpt = app.add_option(
        "--audio-offset", audioOffset, "Audio sample offset in number of samples. Positive value means you are adding a delay (commit in the past).");
    audioOffsetOpt->default_val(0);

    int64_t videoOffset;
    auto videoOffsetOpt = app.add_option(
        "--video-offset", videoOffset, "Video frame offset in number of frames. Positive value means you are adding a delay (commit in the past).");
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
                std::string flowNmosDesc{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                mxl::lib::FlowParser flowNmos{flowNmosDesc};

                if (flowNmos.get<std::string>("interlace_mode") != "progressive")
                {
                    throw std::invalid_argument{"This application does not support interlaced flows."};
                }

                auto frame_rate = flowNmos.getGrainRate();

                std::string flowOptions;
                if (!videoFlowOptionFile.empty())
                {
                    std::ifstream optionFile(videoFlowOptionFile, std::ios::in | std::ios::binary);
                    if (!optionFile)
                    {
                        MXL_ERROR("Failed to open file: '{}'", videoFlowOptionFile);
                        return EXIT_FAILURE;
                    }
                    flowOptions = {std::istreambuf_iterator<char>(optionFile), std::istreambuf_iterator<char>()};
                }

                auto frameHeight = static_cast<uint64_t>(flowNmos.get<double>("frame_height"));

                auto mxlWriter = MxlWriter(domain, flowNmosDesc, flowOptions);
                if (mxlWriter.maxCommitBatchSizeHint() > frameHeight)
                {
                    throw std::invalid_argument{"slicesPerBatch cannot be greater than frame height."};
                }

                VideoPipelineConfig gst_config{
                    .frame_width = static_cast<uint64_t>(flowNmos.get<double>("frame_width")),
                    .frame_height = frameHeight,
                    .frame_rate = frame_rate,
                    .pattern = pattern_map.at(pattern),
                    .textoverlay = textOverlay,
                    .sliceSize = flowNmos.getPayloadSliceLengths()[0],
                };

                VideoPipeline gst_pipeline(gst_config);

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
                std::string flowNmosDesc{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                mxl::lib::FlowParser flowNmos{flowNmosDesc};

                std::string flowOptions;
                if (!audioFlowOptionFile.empty())
                {
                    std::ifstream optionFile(audioFlowOptionFile, std::ios::in | std::ios::binary);
                    if (!optionFile)
                    {
                        MXL_ERROR("Failed to open file: '{}'", audioFlowOptionFile);
                        return EXIT_FAILURE;
                    }
                    flowOptions = {std::istreambuf_iterator<char>(optionFile), std::istreambuf_iterator<char>()};
                }

                auto mxlWriter = MxlWriter(domain, flowNmosDesc, flowOptions);

                AudioPipelineConfig gst_config{
                    .rate = flowNmos.getGrainRate(),
                    .channelCount = flowNmos.getChannelCount(),
                    .samplesPerBatch = mxlWriter.maxCommitBatchSizeHint(),
                    .wave = wave_map.at("sine"),
                };

                AudioPipeline gst_pipeline(gst_config);

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
