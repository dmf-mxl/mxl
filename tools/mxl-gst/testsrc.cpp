// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <glib.h>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/gstsystemclock.h>
#include <picojson/wrapper.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "utils.hpp"

namespace
{
    auto volatile g_exit_requested = std::sig_atomic_t{0};

    void signal_handler(int) noexcept
    {
        g_exit_requested = 1;
    }

    struct VideoPipelineConfig
    {
        std::uint64_t frameWidth{1920};
        std::uint64_t frameHeight{1080};
        mxlRational frameRate{30, 1};
        std::uint64_t pattern{0};
        std::string textOverlay{"EBU DMF MXL"};
        std::uint32_t sliceSize;

        [[nodiscard]]
        std::string display() const
        {
            return fmt::format("frameWidth={} frameHeight={} frameRate={}/{} pattern={} textOverlay={}",
                frameWidth,
                frameHeight,
                frameRate.numerator,
                frameRate.denominator,
                pattern,
                textOverlay);
        }
    };

    struct AudioPipelineConfig
    {
        mxlRational sampleRate{48000, 1};
        std::size_t channelCount{2};
        std::uint32_t samplesPerBatch;
        std::uint64_t wave{0};
        std::int64_t offset{0};

        [[nodiscard]]
        std::string display() const
        {
            return fmt::format(
                "sampleRate={} channelCount={}  samplesPerBatch={} wave={}", sampleRate.numerator, channelCount, samplesPerBatch, wave);
        }
    };

    auto const pattern_map = std::unordered_map<std::string, std::uint64_t>{
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

    auto const wave_map = std::unordered_map<std::string, std::uint64_t>{
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

    class GstreamerSampleRef
    {
    public:
        constexpr GstreamerSampleRef() noexcept
            : _sample{}
            , _buffer{}
        {}

        constexpr GstreamerSampleRef(GstreamerSampleRef&& other) noexcept
            : GstreamerSampleRef{}
        {
            swap(other);
        }

        GstreamerSampleRef(GstreamerSampleRef const& other) noexcept
            : GstreamerSampleRef{}
        {
            if (other._buffer)
            {
                _buffer = ::gst_buffer_ref(other._buffer);
            }
            if (other._sample)
            {
                _sample = ::gst_sample_ref(other._sample);
            }
        }

        constexpr explicit GstreamerSampleRef(GstSample* sample, GstBuffer* buffer) noexcept
            : _sample{sample}
            , _buffer{buffer}
        {}

        GstreamerSampleRef& operator=(GstreamerSampleRef other) noexcept
        {
            swap(other);
            return *this;
        }

        constexpr explicit operator bool() const noexcept
        {
            return (_sample != nullptr) && (_buffer != nullptr);
        }

        [[nodiscard]]
        constexpr GstSample* sample() const noexcept
        {
            return _sample;
        }

        [[nodiscard]]
        constexpr GstBuffer* buffer() const noexcept
        {
            return _buffer;
        }

        constexpr void swap(GstreamerSampleRef& other) noexcept
        {
            {
                auto const temp = _sample;
                _sample = other._sample;
                other._sample = temp;
            }
            {
                auto const temp = _buffer;
                _buffer = other._buffer;
                other._buffer = temp;
            }
        }

        ~GstreamerSampleRef()
        {
            if (_buffer)
            {
                ::gst_buffer_unref(_buffer);
            }
            if (_sample)
            {
                ::gst_sample_unref(_sample);
            }
        }

    private:
        GstSample* _sample;
        GstBuffer* _buffer;
    };

    [[maybe_unused]]
    constexpr void swap(GstreamerSampleRef& lhs, GstreamerSampleRef& rhs) noexcept
    {
        lhs.swap(rhs);
    }

    class GstreamerPipeline
    {
    public:
        void start()
        {
            if (_pipeline == nullptr)
            {
                throw std::runtime_error{"Attempt to start uninitialized pipeline."};
            }

            // Start playing
            ::gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            // Here we want to align the gstreamer pipeline as close as possible
            // to an epoch grain. To achieve this, we set the base time to the
            // next grain timestamp.
            _mxlBaseTime = mxlIndexToTimestamp(&_grainRate, mxlGetCurrentIndex(&_grainRate) + 1U);
            MXL_INFO("Staring pipeline with base time: {} ns", _mxlBaseTime);
        }

        GstreamerSampleRef pull(std::uint64_t timeout) noexcept
        {
            auto result = GstreamerSampleRef{};

            if (_appSink)
            {
                if (auto const sample = ::gst_app_sink_try_pull_sample(GST_APP_SINK(_appSink), timeout); sample)
                {
                    if (auto const buffer = ::gst_sample_get_buffer(sample); buffer)
                    {
                        ::gst_buffer_ref(buffer);
                        result = GstreamerSampleRef{sample, buffer};

                        // Change the PTS to TAI time and include preroll delay
                        auto const bufferTs = GST_BUFFER_PTS(buffer);
                        GST_BUFFER_PTS(buffer) = _mxlBaseTime + bufferTs;
                    }
                    else
                    {
                        ::gst_sample_unref(sample);
                    }
                }
            }
            return result;
        }

    protected:
        GstreamerPipeline() noexcept
            : _grainRate{}
            , _pipeline{nullptr}
            , _appSink{nullptr}
            , _mxlBaseTime{0}
        {}

        ~GstreamerPipeline()
        {
            if (_appSink)
            {
                if (GST_OBJECT_REFCOUNT_VALUE(_appSink))
                {
                    ::gst_object_unref(_appSink);
                }
            }
            if (_pipeline)
            {
                ::gst_element_set_state(_pipeline, GST_STATE_NULL);
                ::gst_object_unref(_pipeline);
            }
        }

        void launchPipeline(std::string const& pipelineDescription, mxlRational grainRate)
        {
            _grainRate = grainRate;

            GError* error = nullptr;
            _pipeline = ::gst_parse_launch(pipelineDescription.c_str(), &error);
            if ((_pipeline == nullptr) || (error != nullptr))
            {
                if (error != nullptr)
                {
                    MXL_ERROR("Failed to launch pipeline: {}", error->message);
                    ::g_error_free(error);
                }

                throw std::runtime_error{"Gstreamer pipeline could not be created."};
            }

            _appSink = ::gst_bin_get_by_name(GST_BIN(_pipeline), "appsink");
            if (_appSink == nullptr)
            {
                throw std::runtime_error{"Well-known application sink element could not be found in the gstreamer pipeline."};
            }

            // The clock returned by gst_pipeline_get_clock() is not guaranteed to be of type
            // GstSystemClock, which would make setting the clock-type a noop. So we create a
            // new clock with the necessary type and make the pipeline use that.
            if (auto const clock = GST_CLOCK(::g_object_new(GST_TYPE_SYSTEM_CLOCK, "name", "mxl-tai-clock", nullptr)); clock != nullptr)
            {
                ::gst_object_ref_sink(clock);
                ::g_object_set(G_OBJECT(clock), "clock-type", GST_CLOCK_TYPE_TAI, nullptr);
                ::gst_clock_wait_for_sync(clock, GST_CLOCK_TIME_NONE);
                ::gst_pipeline_use_clock(GST_PIPELINE(_pipeline), clock);
                ::gst_object_unref(clock);
            }
            else
            {
                throw std::runtime_error{"Could not create pipeline TAI clock."};
            }
        }

        [[nodiscard]]
        constexpr GstElement* getAppSink() noexcept
        {
            return _appSink;
        }

        [[nodiscard]]
        constexpr GstElement const* getAppSink() const noexcept
        {
            return _appSink;
        }

    private:
        mxlRational _grainRate;
        GstElement* _pipeline;
        GstElement* _appSink;
        std::uint64_t _mxlBaseTime;
    };

    class VideoPipeline : public GstreamerPipeline
    {
    public:
        VideoPipeline(VideoPipelineConfig const& config)
            : GstreamerPipeline{}
            , _config{config}
        {
            MXL_INFO("Creating video pipeline with config: {}", _config.display());

            auto pipelineDesc = fmt::format(
                "videotestsrc name=videotestsrc is-live=true do-timestamp=true pattern={} ! "
                "video/x-raw,format=v210,width={},height={},framerate={}/{} ! "
                "textoverlay text=\"{}\" font-desc=\"Sans, 36\" ! "
                "clockoverlay ! "
                "videoconvert ! "
                "videoscale ! "
                "queue ! "
                "appsink name=appsink ",
                _config.pattern,
                _config.frameWidth,
                _config.frameHeight,
                _config.frameRate.numerator,
                _config.frameRate.denominator,
                _config.textOverlay);

            MXL_INFO("Generating following gsteamer video pipeline -> {}", pipelineDesc);
            launchPipeline(pipelineDesc, _config.frameRate);

            // Configure appsink
            auto const appSink = getAppSink();
            ::g_object_set(G_OBJECT(appSink),
                "caps",
                ::gst_caps_new_simple("video/x-raw",
                    "format",
                    G_TYPE_STRING,
                    "v210",
                    "width",
                    G_TYPE_INT,
                    _config.frameWidth,
                    "height",
                    G_TYPE_INT,
                    _config.frameHeight,
                    "framerate",
                    GST_TYPE_FRACTION,
                    _config.frameRate.numerator,
                    _config.frameRate.denominator,
                    nullptr),
                nullptr);
        }

        [[nodiscard]]
        VideoPipelineConfig const& config() const noexcept
        {
            return _config;
        }

    private:
        VideoPipelineConfig _config;
    };

    class AudioPipeline : public GstreamerPipeline
    {
    public:
        AudioPipeline(AudioPipelineConfig const& config)
            : GstreamerPipeline{}
            , _config{config}
        {
            MXL_INFO("Creating audio pipeline with config: {}", config.display());

            auto samplesPerBatch = config.samplesPerBatch;

            auto pipelineDesc = std::string{};
            for (auto chan = std::size_t{0}; chan < config.channelCount; ++chan)
            {
                auto const freq = (chan + 1U) * 100U;
                pipelineDesc += fmt::format(
                    "audiotestsrc is-live=true do-timestamp=true freq={} samplesperbuffer={} ! audio/x-raw,channels=1 ! queue ! m.sink_{} ",
                    freq,
                    samplesPerBatch,
                    chan);
            }
            pipelineDesc += fmt::format("interleave name=m ! \
                         audioconvert ! \
                         queue !\
                         audio/x-raw,format=F32LE,rate={},channels={},layout=non-interleaved ! \
                         queue ! \
                         appsink name=appsink",
                config.sampleRate.numerator,
                config.channelCount);

            MXL_INFO("Generating following gsteamer audio pipeline -> {}", pipelineDesc);
            launchPipeline(pipelineDesc, _config.sampleRate);
        }

        [[nodiscard]]
        AudioPipelineConfig const& config() const noexcept
        {
            return _config;
        }

    private:
        AudioPipelineConfig _config;
    };

    class MxlWriter
    {
    public:
        MxlWriter(std::string const& domain, std::string const& flow_descriptor, std::string const& flowOptions)
            // Delegate to the default ctor. See comment below on why we do that
            : MxlWriter{}
        {
            _instance = ::mxlCreateInstance(domain.c_str(), "");
            if (_instance == nullptr)
            {
                throw std::runtime_error{"Failed to create MXL instance."};
            }

            bool flowWasCreated = false;
            if (::mxlCreateFlowWriter(_instance, flow_descriptor.c_str(), flowOptions.c_str(), &_writer, &_configInfo, &flowWasCreated) !=
                MXL_STATUS_OK)
            {
                throw std::runtime_error{"Failed to create flow writer."};
            }
            if (!flowWasCreated)
            {
                MXL_WARN("Reusing existing flow.");
            }
        }

        ~MxlWriter()
        {
            if (_writer != nullptr)
            {
                ::mxlReleaseFlowWriter(_instance, _writer);
            }

            if (_instance != nullptr)
            {
                ::mxlDestroyInstance(_instance);
            }
        }

        [[nodiscard]]
        std::uint32_t maxCommitBatchSizeHint() const noexcept
        {
            return _configInfo.common.maxCommitBatchSizeHint;
        }

        void run(VideoPipeline& gstPipeline, std::int64_t offset)
        {
            if (_configInfo.common.format != MXL_DATA_FORMAT_VIDEO)
            {
                throw std::domain_error{"Attempt to feed a non-video MXL flow from a gstreamer video pipeline."};
            }

            gstPipeline.start();

            auto grainIndex = std::uint64_t{MXL_UNDEFINED_INDEX};

            auto slicesPerBatch = _configInfo.common.maxCommitBatchSizeHint;
            while (!g_exit_requested)
            {
                auto const timeoutNs = (grainIndex != MXL_UNDEFINED_INDEX) ? ::mxlGetNsUntilIndex(grainIndex + 1, &gstPipeline.config().frameRate)
                                                                           : 100'000'000ULL; // arbitrary high timeout for the first grain
                if (auto const pipelineSample = gstPipeline.pull(timeoutNs); pipelineSample)
                {
                    auto const bufferTs = GST_BUFFER_PTS(pipelineSample.buffer()); // PTS was already converted to TAI time in the pull()
                    auto const gstGrainIndex = ::mxlTimestampToIndex(&gstPipeline.config().frameRate, bufferTs);

                    // First buffer, set the initial grain index
                    if (grainIndex == MXL_UNDEFINED_INDEX)
                    {
                        grainIndex = gstGrainIndex;
                        MXL_INFO("DiscreteFlow: Set initial grain index to {} (bufferTs={} ns)", grainIndex, bufferTs);
                    }

                    // verify that we didn't miss any grains
                    if (gstGrainIndex < grainIndex) // gstreamer index is smaller than we expected. time went backward??
                    {
                        MXL_ERROR(
                            "Unexpected grain index from gstreamer PTS {} expected grain index {}. Time went backward??", gstGrainIndex, grainIndex);
                    }
                    else if (gstGrainIndex > grainIndex) // gstreamer index is bigger than we expected
                    {
                        MXL_WARN("DiscreteFlow: Skipped grain(s). Expected grain index {}, got grain index {} (bufferTs={} ns). Generating {} "
                                 "skipped grains",
                            grainIndex,
                            gstGrainIndex,
                            bufferTs,
                            gstGrainIndex - grainIndex);

                        // Generate the skipped grains as invalid grains
                        while (grainIndex < gstGrainIndex)
                        {
                            auto gInfo = mxlGrainInfo{};
                            std::uint8_t* mxlBuffer = nullptr;
                            auto const actualGrainIndex = grainIndex - offset;

                            /// Open the grain for writing.
                            if (::mxlFlowWriterOpenGrain(_writer, actualGrainIndex, &gInfo, &mxlBuffer) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to open grain at index '{}'", actualGrainIndex);
                                break;
                            }

                            gInfo.flags = MXL_GRAIN_FLAG_INVALID;
                            if (::mxlFlowWriterCommitGrain(_writer, &gInfo) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to commit invalid grain at index '{}'", actualGrainIndex);
                                break;
                            }

                            grainIndex += 1U;
                        }
                    }
                    else // gstreamer index matches expected index
                    {
                        auto const actualGrainIndex = grainIndex - offset;

                        auto const nbBatches = (gstPipeline.config().frameHeight + (slicesPerBatch - 1)) /
                                               slicesPerBatch; // Calculate how many batches are required to commit the full grain.

                        auto mapInfo = ::GstMapInfo{};
                        if (::gst_buffer_map(pipelineSample.buffer(), &mapInfo, GST_MAP_READ))
                        {
                            /// Open the grain.
                            auto gInfo = ::mxlGrainInfo{};
                            std::uint8_t* mxlBuffer = nullptr;

                            /// Open the grain for writing.
                            if (::mxlFlowWriterOpenGrain(_writer, actualGrainIndex, &gInfo, &mxlBuffer) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to open grain at index '{}'", actualGrainIndex);
                                ::gst_buffer_unmap(pipelineSample.buffer(), &mapInfo);
                                break;
                            }

                            gInfo.validSlices = 0;

                            auto const sliceSize = gstPipeline.config().sliceSize;

                            auto sleepTimeBetweenBatches = std::uint64_t{0};
                            if (nbBatches > 1)
                            {
                                // spread the batches across half a frame period.
                                sleepTimeBetweenBatches = 1'000'000'000ULL * gstPipeline.config().frameRate.denominator /
                                                          gstPipeline.config().frameRate.numerator / (nbBatches * 2);

                                // Any sleep under 50us will most likely not work. If it's the case, just skip the sleeps.
                                if (sleepTimeBetweenBatches < 50'000ULL)
                                {
                                    sleepTimeBetweenBatches = 0;
                                }
                            }

                            for (auto i = std::size_t{0}; i < nbBatches; ++i)
                            {
                                auto const nbSlices = std::min<std::uint16_t>(slicesPerBatch, gInfo.totalSlices - gInfo.validSlices);

                                auto const batchOffset = sliceSize * slicesPerBatch * i;
                                ::memcpy(mxlBuffer + batchOffset, mapInfo.data + batchOffset, nbSlices * sliceSize);
                                gInfo.validSlices += nbSlices;

                                if (::mxlFlowWriterCommitGrain(_writer, &gInfo) != MXL_STATUS_OK)
                                {
                                    MXL_ERROR("Failed to commit grain at index '{}'", actualGrainIndex);
                                    break;
                                }

                                ::mxlSleepForNs(sleepTimeBetweenBatches);
                            }

                            ::gst_buffer_unmap(pipelineSample.buffer(), &mapInfo);
                        }
                        else
                        {
                            MXL_WARN("Failed to map gst buffer for grain index '{}'", actualGrainIndex);
                        }
                    }

                    grainIndex += 1U;
                    ::mxlSleepForNs(::mxlGetNsUntilIndex(grainIndex, &gstPipeline.config().frameRate));
                }
            }
        }

        void run(AudioPipeline& gstPipeline, std::int64_t offset)
        {
            if (_configInfo.common.format != MXL_DATA_FORMAT_AUDIO)
            {
                throw std::domain_error{"Attempt to feed a non-audio MXL flow from a gstreamer audio pipeline."};
            }

            gstPipeline.start();

            auto const samplesPerBatch = gstPipeline.config().samplesPerBatch;

            auto sampleIndex = std::uint64_t{MXL_UNDEFINED_INDEX};
            while (!g_exit_requested)
            {
                auto const timeoutNs = (sampleIndex != MXL_UNDEFINED_INDEX)
                                         ? mxlGetNsUntilIndex(sampleIndex + samplesPerBatch, &gstPipeline.config().sampleRate)
                                         : 100'000'000ULL; // arbitrary high timeout for the first sample

                if (auto const pipelineSample = gstPipeline.pull(timeoutNs); pipelineSample)
                {
                    auto const bufferTs = GST_BUFFER_PTS(pipelineSample.buffer()); // PTS was already converted to TAI time in the pull()
                    auto const gstSampleIndex = ::mxlTimestampToIndex(&gstPipeline.config().sampleRate, bufferTs);

                    // First buffer, set the initial sample index
                    if (sampleIndex == MXL_UNDEFINED_INDEX)
                    {
                        sampleIndex = gstSampleIndex;
                        MXL_INFO("ContinuousFlow: Set initial sample index to {} (bufferTs={} ns)", sampleIndex, bufferTs);
                    }
                    // Verify that we didn't miss any samples
                    if (gstSampleIndex < sampleIndex) // gstreamer index is smaller than we expected. time went backward??
                    {
                        MXL_ERROR("Unexpected sample index from gstreamer PTS {} expected sample index {}. Time went backward??",
                            gstSampleIndex,
                            sampleIndex);
                    }
                    else if (gstSampleIndex > sampleIndex) // gstreamer index is bigger than we expected
                    {
                        MXL_WARN("ContinuousFlow: Skipped sample(s). Expected sample index {}, got sample index {} (bufferTs={} ns). Generating {} "
                                 "silenced samples",
                            sampleIndex,
                            gstSampleIndex,
                            bufferTs,
                            gstSampleIndex - sampleIndex);

                        // Generate the skipped samples as silenced samples.
                        // ** A production application should apply a fade when inserting silence to avoid audio artefacts
                        auto const nbSamples = gstSampleIndex - sampleIndex;
                        auto const actualSampleIndex = gstSampleIndex - offset;

                        auto payloadBuffersSlices = mxlMutableWrappedMultiBufferSlice{};
                        if (::mxlFlowWriterOpenSamples(_writer, actualSampleIndex, nbSamples, &payloadBuffersSlices) != MXL_STATUS_OK)
                        {
                            MXL_ERROR("Failed to open samples at index '{}'", actualSampleIndex);
                            break;
                        }

                        for (auto chan = std::size_t{0}; chan < payloadBuffersSlices.count; ++chan)
                        {
                            for (auto& fragment : payloadBuffersSlices.base.fragments)
                            {
                                if (fragment.size != 0)
                                {
                                    auto const dst = static_cast<std::uint8_t*>(fragment.pointer) + (chan * payloadBuffersSlices.stride);
                                    ::memset(dst, 0, fragment.size); // fill with silence
                                }
                            }
                        }
                    }
                    else // gstreamer index matches expected index
                    {
                        auto const actualSampleIndex = sampleIndex - offset;

                        auto mapInfo = GstMapInfo{};
                        if (::gst_buffer_map(pipelineSample.buffer(), &mapInfo, GST_MAP_READ))
                        {
                            auto const nbSamplesPerChan = mapInfo.size / (4 * _configInfo.continuous.channelCount);

                            auto payloadBuffersSlices = ::mxlMutableWrappedMultiBufferSlice{};
                            if (::mxlFlowWriterOpenSamples(_writer, actualSampleIndex, nbSamplesPerChan, &payloadBuffersSlices) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to open samples at index '{}'", actualSampleIndex);
                                ::gst_buffer_unmap(pipelineSample.buffer(), &mapInfo);
                                break;
                            }

                            auto addrOffset = std::ptrdiff_t{0};
                            for (auto chan = std::size_t{0}; chan < payloadBuffersSlices.count; ++chan)
                            {
                                for (auto& fragment : payloadBuffersSlices.base.fragments)
                                {
                                    if (fragment.size != 0)
                                    {
                                        auto const dst = static_cast<std::uint8_t*>(fragment.pointer) + (chan * payloadBuffersSlices.stride);
                                        auto const src = mapInfo.data + addrOffset;
                                        ::memcpy(dst, src, fragment.size);

                                        addrOffset += fragment.size;
                                    }
                                }
                            }

                            if (::mxlFlowWriterCommitSamples(_writer) != MXL_STATUS_OK)
                            {
                                MXL_ERROR("Failed to open samples at index '{}'", actualSampleIndex);
                                ::gst_buffer_unmap(pipelineSample.buffer(), &mapInfo);
                                break;
                            }
                        }

                        ::gst_buffer_unmap(pipelineSample.buffer(), &mapInfo);
                    }

                    sampleIndex += samplesPerBatch;
                    mxlSleepForNs(mxlGetNsUntilIndex(sampleIndex, &gstPipeline.config().sampleRate));
                }
            }
        }

    private:
        /**
         * This default constructor is private, because the only reason why it
         * exists is that it can be used by the publicly accessible constructor
         * to delegate to.
         * This delegation is beneficial, because it implies that the destructor
         * will be invoked when the delgating constructor throws and exception,
         * which is what we need in order to clean up partially established
         * state.
         */
        constexpr MxlWriter() noexcept
            : _instance{}
            , _writer{}
            , _configInfo{}
        {}

    private:
        mxlInstance _instance;

        mxlFlowWriter _writer;
        mxlFlowConfigInfo _configInfo;
    };

    std::string readFile(std::string const& filepath)
    {
        if (auto file = std::ifstream{filepath, std::ios::in | std::ios::binary}; file)
        {
            auto reader = std::stringstream{};
            reader << file.rdbuf();
            return reader.str();
        }

        throw std::runtime_error{"Failed to open file: " + filepath};
    }
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    auto app = CLI::App{"mxl-gst-testsrc"};

    auto videoFlowConfigFile = std::string{};
    auto videoFlowConfigFileOpt = app.add_option(
        "-v, --video-config-file", videoFlowConfigFile, "The json file which contains the Video NMOS Flow configuration.");
    videoFlowConfigFileOpt->default_val("");

    auto videoFlowOptionFile = std::string{};
    auto videoFlowOptionFileOpt = app.add_option("--video-options-file", videoFlowOptionFile, "The json file which contains the Video Flow options.");
    videoFlowOptionFileOpt->default_val("");

    auto audioFlowConfigFile = std::string{};
    auto audioFlowConfigFileOpt = app.add_option(
        "-a, --audio-config-file", audioFlowConfigFile, "The json file which contains the Audio NMOS Flow configuration");
    audioFlowConfigFileOpt->default_val("");

    auto audioFlowOptionFile = std::string{};
    auto audioFlowOptionFileOpt = app.add_option("--audio-options-file", audioFlowOptionFile, "The json file which contains the Audio Flow options.");
    audioFlowOptionFileOpt->default_val("");

    auto audioOffset = std::int64_t{};
    auto audioOffsetOpt = app.add_option(
        "--audio-offset", audioOffset, "Audio sample offset in number of samples. Positive value means you are adding a delay (commit in the past).");
    audioOffsetOpt->default_val(0);

    auto videoOffset = std::int64_t{};
    auto videoOffsetOpt = app.add_option(
        "--video-offset", videoOffset, "Video frame offset in number of frames. Positive value means you are adding a delay (commit in the past).");
    videoOffsetOpt->default_val(0);

    auto domain = std::string{};
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    auto pattern = std::string{};
    auto patternOpt = app.add_option("-p, --pattern",
        pattern,
        "Test pattern to use. For available options see "
        "https://gstreamer.freedesktop.org/documentation/videotestsrc/index.html?gi-language=c#GstVideoTestSrcPattern");
    patternOpt->default_val("smpte");

    auto textOverlay = std::string{};
    auto textOverlayOpt = app.add_option("-t,--overlay-text", textOverlay, "Change the text overlay of the test source");
    textOverlayOpt->default_val("EBU DMF MXL");

    auto groupHint = std::string{};
    auto groupHintOpt = app.add_option("-g, --group-hint", groupHint, "The group-hint value to use in the flow json definition");
    groupHintOpt->default_val("mxl-gst-testsrc-group");

    CLI11_PARSE(app, argc, argv);

    ::gst_init(nullptr, nullptr);

    auto threads = std::vector<std::thread>{};

    if (!videoFlowConfigFile.empty())
    {
        threads.emplace_back(
            [&]()
            {
                try
                {
                    auto flowNmos = json_utils::parseFile(videoFlowConfigFile);
                    auto const flowOptions = !videoFlowOptionFile.empty() ? readFile(videoFlowOptionFile) : std::string{};
                    if (json_utils::getField<std::string>(flowNmos, "interlace_mode") != "progressive")
                    {
                        throw std::invalid_argument{"This application does not support interlaced flows."};
                    }

                    json_utils::updateGroupHint(flowNmos, groupHint, "video");
                    auto mxlWriter = MxlWriter{domain, json_utils::serializeJson(flowNmos), flowOptions};
                    auto const frameWidth = static_cast<std::uint64_t>(json_utils::getField<double>(flowNmos, "frame_width"));
                    auto const frameHeight = static_cast<std::uint64_t>(json_utils::getField<double>(flowNmos, "frame_height"));
                    if (mxlWriter.maxCommitBatchSizeHint() > frameHeight)
                    {
                        throw std::invalid_argument{"slicesPerBatch cannot be greater than frame height."};
                    }

                    auto const gstConfig = VideoPipelineConfig{.frameWidth = frameWidth,
                        .frameHeight = frameHeight,
                        .frameRate = json_utils::getRational(flowNmos, "grain_rate"),
                        .pattern = pattern_map.at(pattern),
                        .textOverlay = textOverlay,
                        .sliceSize = media_utils::getV210LineLength(frameWidth)};

                    auto gstPipeline = VideoPipeline{gstConfig};

                    mxlWriter.run(gstPipeline, videoOffset);

                    MXL_INFO("Video pipeline finished");
                }
                catch (std::exception const& e)
                {
                    MXL_ERROR("Error while processing video pipeline: {}", e.what());
                }
                catch (...)
                {
                    MXL_ERROR("Encountered unknown error while processing video pipeline.");
                }
            });
    }

    if (!audioFlowConfigFile.empty())
    {
        threads.emplace_back(
            [&]()
            {
                try
                {
                    auto flowNmos = json_utils::parseFile(audioFlowConfigFile);
                    auto const flowOptions = !audioFlowOptionFile.empty() ? readFile(audioFlowOptionFile) : std::string{};
                    json_utils::updateGroupHint(flowNmos, groupHint, "audio");

                    auto mxlWriter = MxlWriter{domain, json_utils::serializeJson(flowNmos), flowOptions};

                    auto const gstConfig = AudioPipelineConfig{
                        .sampleRate = json_utils::getRational(flowNmos, "sample_rate"),
                        .channelCount = static_cast<std::uint32_t>(json_utils::getField<double>(flowNmos, "channel_count")),
                        .samplesPerBatch = mxlWriter.maxCommitBatchSizeHint(),
                        .wave = wave_map.at("sine"),
                    };

                    auto gstPipeline = AudioPipeline{gstConfig};
                    mxlWriter.run(gstPipeline, audioOffset);
                    MXL_INFO("Audio pipeline finished");
                }
                catch (std::exception const& e)
                {
                    MXL_ERROR("Error while processing audio pipeline: {}", e.what());
                }
                catch (...)
                {
                    MXL_ERROR("Encountered unknown error while processing audio pipeline.");
                }
            });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    ::gst_deinit();

    return 0;
}
