// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <glib-object.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstclock.h>
#include <gst/gstpipeline.h>
#include <gst/gstsystemclock.h>
#include <picojson/wrapper.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl/rational.h"
#include "utils.hpp"

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

namespace
{
    auto volatile g_exit_requested = std::sig_atomic_t{0};

    void signal_handler(int) noexcept
    {
        g_exit_requested = 1;
    }

    struct VideoPipelineConfig
    {
        mxlRational frameRate;
        std::uint64_t frameWidth;
        std::uint64_t frameHeight;
        std::int64_t offset;

        [[nodiscard]]
        std::string display() const
        {
            return fmt::format(
                "frameWidth={} frameHeight={} frameRate={}/{} offset={}", frameWidth, frameHeight, frameRate.numerator, frameRate.denominator, offset);
        }
    };

    struct AudioPipelineConfig
    {
        mxlRational sampleRate;
        std::size_t channelCount;
        std::int64_t offset;
        std::vector<std::size_t> speakerChannels;

        [[nodiscard]]
        std::string display() const
        {
            return fmt::format("sampleRate={} channelCount={} offset={} speakerChannels=[{}]",
                sampleRate.numerator,
                channelCount,
                offset,
                fmt::join(speakerChannels, ", "));
        }
    };

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
            _mxlBaseTime = ::mxlGetTime();
            MXL_INFO("Staring pipeline with base time: {} ns", _mxlBaseTime);
        }

        void pushBuffer(GstBuffer* buffer, std::uint64_t now) noexcept
        {
            GST_BUFFER_PTS(buffer) = now - _mxlBaseTime;

            int ret;
            ::g_signal_emit_by_name(_appSource, "push-buffer", buffer, &ret);
            if (ret != GST_FLOW_OK)
            {
                MXL_ERROR("Could not push buffer to application source.");
            }
        }

    protected:
        GstreamerPipeline() noexcept
            : _pipeline{nullptr}
            , _appSource{nullptr}
            , _mxlBaseTime{0}
        {}

        ~GstreamerPipeline()
        {
            if (_pipeline)
            {
                ::gst_element_set_state(_pipeline, GST_STATE_NULL);
                ::gst_object_unref(_pipeline);
            }
            if (_appSource)
            {
                if (GST_OBJECT_REFCOUNT_VALUE(_appSource))
                {
                    ::gst_object_unref(_appSource);
                }
            }
        }

        void launchPipeline(std::string const& pipelineDescription)
        {
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

            _appSource = ::gst_bin_get_by_name(GST_BIN(_pipeline), "appsource");
            if (_appSource == nullptr)
            {
                throw std::runtime_error{"Well-known application source element could not be found in the gstreamer pipeline."};
            }

            ::g_object_set(G_OBJECT(_appSource), "format", GST_FORMAT_TIME, nullptr);

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
        constexpr GstElement* getAppSource() noexcept
        {
            return _appSource;
        }

        [[nodiscard]]
        constexpr GstElement const* getAppSource() const noexcept
        {
            return _appSource;
        }

    private:
        GstElement* _pipeline;
        GstElement* _appSource;
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
                "appsrc name=appsource is-live=true ! "
                "video/x-raw,format=v210,width={},height={},framerate={}/{} ! "
                "videoconvert ! "
                "videoscale ! "
                "autovideosink ts-offset={}",
                _config.frameWidth,
                _config.frameHeight,
                _config.frameRate.numerator,
                _config.frameRate.denominator,
                _config.offset);

            MXL_INFO("Generating following Video gsteamer pipeline -> {}", pipelineDesc);
            launchPipeline(pipelineDesc);
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
            , _audioInfo{}
        {
            MXL_INFO("Creating audio pipeline with config: {}", _config.display());

            auto const pipelineDesc = fmt::format(
                "appsrc name=appsource is-live=true ! "
                "audio/x-raw,format=F32LE,layout=non-interleaved,channels={},rate={} ! "
                "audioconvert mix-matrix=\"{}\" ! "
                "autoaudiosink ts-offset={}",
                _config.channelCount,
                _config.sampleRate.numerator,
                generateMixMatrix(),
                _config.offset);

            MXL_INFO("Generating following Audio gsteamer pipeline -> {}", pipelineDesc);
            launchPipeline(pipelineDesc);

            {
                auto channelPositions = std::vector<GstAudioChannelPosition>{config.channelCount};

                auto index = 0;
                for (auto& pos : channelPositions)
                {
                    pos = static_cast<GstAudioChannelPosition>(index++);
                }

                _audioInfo = ::gst_audio_info_new();
                ::gst_audio_info_set_format(
                    _audioInfo, GST_AUDIO_FORMAT_F32LE, config.sampleRate.numerator, config.channelCount, channelPositions.data());
                _audioInfo->layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;

                auto const caps = ::gst_audio_info_to_caps(_audioInfo);
                ::g_object_set(G_OBJECT(getAppSource()), "caps", caps, nullptr);
            }
        }

        ~AudioPipeline()
        {
            if (_audioInfo)
            {
                ::gst_audio_info_free(_audioInfo);
            }
        }

        [[nodiscard]]
        AudioPipelineConfig const& config() const noexcept
        {
            return _config;
        }

        [[nodiscard]]
        GstAudioInfo const* audioInfo() const noexcept
        {
            return _audioInfo;
        }

    private:
        std::string generateMixMatrix()
        {
            auto out = std::stringstream{};

            out << '<';
            for (auto speakerIndex = std::size_t{0}; speakerIndex < _config.speakerChannels.size(); ++speakerIndex)
            {
                if (speakerIndex > 0)
                {
                    out << ',';
                }

                auto const speakerChannel = _config.speakerChannels[speakerIndex];

                out << '<';
                for (auto channelIndex = std::size_t{0}; channelIndex < _config.channelCount; ++channelIndex)
                {
                    if (channelIndex > 0)
                    {
                        out << ',';
                    }
                    out << "(float)" << ((channelIndex == speakerChannel) ? '1' : '0');
                }
                out << '>';
            }
            out << '>';

            return out.str();
        }

    private:
        AudioPipelineConfig _config;
        GstAudioInfo* _audioInfo;
    };

    class MxlReader
    {
    public:
        MxlReader(std::string const& domain, std::string flowId)
            // Delegate to the default ctor. See comment below on why we do that
            : MxlReader{}
        {
            _instance = mxlCreateInstance(domain.c_str(), "");
            if (_instance == nullptr)
            {
                throw std::runtime_error{"Failed to create MXL instance"};
            }

            if (auto const ret = ::mxlCreateFlowReader(_instance, flowId.c_str(), "", &_reader); ret != MXL_STATUS_OK)
            {
                throw std::runtime_error{fmt::format("Failed to create flow reader with status code {}", static_cast<int>(ret))};
            }

            if (auto const ret = mxlFlowReaderGetConfigInfo(_reader, &_configInfo); ret != MXL_STATUS_OK)
            {
                throw std::runtime_error{fmt::format("Failed to get flow config info with status code {}", static_cast<int>(ret))};
            }
        }

        ~MxlReader()
        {
            if (_reader != nullptr)
            {
                ::mxlReleaseFlowReader(_instance, _reader);
            }
            if (_instance != nullptr)
            {
                ::mxlDestroyInstance(_instance);
            }
        }

        void run(VideoPipeline& gstPipeline, std::int64_t readDelay)
        {
            if (_configInfo.common.format != MXL_DATA_FORMAT_VIDEO)
            {
                throw std::domain_error{"Attempt to feed a gstreamer video pipeline from a non-video MXL flow."};
            }

            gstPipeline.start();

            auto const rate = _configInfo.common.grainRate;
            auto const slicesPerBatch = _configInfo.common.maxSyncBatchSizeHint;
            auto const sliceReadMode = (slicesPerBatch < gstPipeline.config().frameHeight);
            if (slicesPerBatch > gstPipeline.config().frameHeight)
            {
                throw std::invalid_argument{"slicesPerBatch cannot be greater than frame height."};
            }

            MXL_INFO("Starting discrete flow reading at rate {}/{} and slices per batch {}", rate.numerator, rate.denominator, slicesPerBatch);

            // The index that corresponds to the current time. We are reading
            // frames that correspond to the index that is readOffset
            // nanoseconds in the past and deliver them as the next frame to the
            // gstreamer pipeline.
            auto cursor = Cursor{rate, 1U, readDelay};

            initializeHighestLatency("Video", rate, cursor.currentIndex(), cursor.requestedIndex());

            auto expectedSlices = slicesPerBatch;
            while (!g_exit_requested)
            {
                auto ret = mxlStatus{};
                mxlGrainInfo grainInfo;
                uint8_t* payload;

                auto const iterationStartTime = ::mxlGetTime();
                auto const iterationTimeoutNs =
                    (iterationStartTime < cursor.deliveryDeadline()) ? (cursor.deliveryDeadline() - iterationStartTime) : 0ULL;
                if (sliceReadMode)
                {
                    // Slice mode is not really useful here since gstreamer needs the full grain to push to the pipeline. But for educational
                    // purposes, here's how you can wait for slices to be available.
                    // Please note that -- contrary to how this sample does it -- you don't have to use the slice based reading just because
                    // the producer indicates a sub-grain sync batch size and are free to use mxlFlowReaderGetGrain() in any case.
                    ret = ::mxlFlowReaderGetGrainSlice(_reader, cursor.requestedIndex(), expectedSlices, iterationTimeoutNs, &grainInfo, &payload);
                }
                else
                {
                    // Use this function to wait for a full grain
                    ret = ::mxlFlowReaderGetGrain(_reader, cursor.requestedIndex(), iterationTimeoutNs, &grainInfo, &payload);
                }

                if (ret == MXL_STATUS_OK)
                {
                    if (grainInfo.validSlices >= grainInfo.totalSlices)
                    {
                        if ((grainInfo.flags & MXL_GRAIN_FLAG_INVALID) == 0)
                        {
                            // We've validated the grain. Invalid grains are skipped rather than pushed to GStreamer. Since we provide PTS values
                            // based on MXL timestamps, GStreamer automatically handles missing grains by repeating the last valid frame. Consuming
                            // applications should implement similar logic for invalid grain handling.

                            updateHighestLatency("Video", ::mxlIndexToTimestamp(&rate, cursor.requestedIndex()), ::mxlGetTime());

                            // If we got here, we can push the grain to the gstreamer pipeline
                            auto const buffer = ::gst_buffer_new_allocate(nullptr, grainInfo.grainSize, nullptr);
                            auto map = GstMapInfo{};

                            ::gst_buffer_map(buffer, &map, GST_MAP_WRITE);
                            std::memcpy(map.data, payload, grainInfo.grainSize);
                            ::gst_buffer_unmap(buffer, &map);

                            gstPipeline.pushBuffer(buffer, ::mxlIndexToTimestamp(&rate, cursor.currentIndex() + 1U));

                            ::gst_buffer_unref(buffer);
                        }

                        cursor.next();

                        expectedSlices = slicesPerBatch;
                    }
                    else if (sliceReadMode)
                    {
                        // Calculate up to how many valid slices to wait for the next iteration
                        expectedSlices = std::min<std::uint16_t>(grainInfo.totalSlices, grainInfo.validSlices + slicesPerBatch);
                    }
                }
                else if (ret == MXL_ERR_FLOW_INVALID)
                {
                    if (handleInvalidFlow(cursor.requestedIndex()))
                    {
                        // Realign to current index.
                        cursor.realign(iterationStartTime);
                    }
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                {
                    // We are too early somehow, keep trying the same grain index
                    auto runtimeInfo = ::mxlFlowRuntimeInfo{};
                    (void)::mxlFlowReaderGetRuntimeInfo(_reader, &runtimeInfo);
                    MXL_WARN("Failed to get grain at index {}: TOO EARLY. Last published {}", cursor.requestedIndex(), runtimeInfo.headIndex);
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
                {
                    auto runtimeInfo = ::mxlFlowRuntimeInfo{};
                    (void)::mxlFlowReaderGetRuntimeInfo(_reader, &runtimeInfo);
                    MXL_TRACE("Failed to get grain at index {}: TOO LATE. Last published {}", cursor.requestedIndex(), runtimeInfo.headIndex);

                    // Grain expired. Realign to current index. GStreamer repeats the last valid frame for missing data; consuming applications
                    // should do the same.
                    cursor.realign(iterationStartTime);
                }
                else
                {
                    // On any other error, verify if the _reader is nullptr (which indicates that the flow may have been invalidated).
                    // In that case, try to recreate it. Otherwise, exit.
                    if (_reader == nullptr)
                    {
                        // Create a new reader
                        auto const flowId = uuids::to_string(_configInfo.common.id);
                        if (auto const ret = ::mxlCreateFlowReader(_instance, flowId.c_str(), "", &_reader); ret != MXL_STATUS_OK)
                        {
                            MXL_TRACE("Failed to reopen video flow reader with status code {}.", static_cast<int>(ret));
                            // Arbitrary wait time before retrying to prevent busy looping
                            std::this_thread::sleep_for(std::chrono::milliseconds{500});
                        }
                        else
                        {
                            MXL_INFO("Reconnected to video flowId {}.", flowId);
                            // Get the flow config info again
                            if (auto const ret = mxlFlowReaderGetConfigInfo(_reader, &_configInfo); ret != MXL_STATUS_OK)
                            {
                                // Something is very wrong. we cannot recover from this.
                                MXL_ERROR("Failed to get flow config info with status code {}. Exiting.", static_cast<int>(ret));
                                return;
                            }
                        }

                        // Realign to current index. GStreamer repeats the last valid frame for missing data; consuming applications
                        // should do the same.
                        if (_reader != nullptr)
                        {
                            cursor.realign(iterationStartTime);
                        }
                    }
                    else
                    {
                        MXL_ERROR(
                            "Unexpected error when reading the grain {} with status {}. Exiting...", cursor.requestedIndex(), static_cast<int>(ret));
                        return;
                    }
                }
            }
        }

        void run(AudioPipeline& gstPipeline, std::int64_t readDelay)
        {
            if (_configInfo.common.format != MXL_DATA_FORMAT_AUDIO)
            {
                throw std::domain_error{"Attempt to feed a gstreamer audio pipeline from a non-audio MXL flow."};
            }

            gstPipeline.start();

            auto const rate = _configInfo.common.grainRate;
            // Provide a lower bound for the window size to keep the overhead per sample reasonably low
            auto const windowSize = std::max(_configInfo.common.maxSyncBatchSizeHint, 48U);

            MXL_INFO("Starting continuous flow reading at rate {}/{} and batch size {}", rate.numerator, rate.denominator, windowSize);

            // The index that corresponds to the current time. We are reading a range of samples up to the
            // index that is readOffset nanoseconds in the past and deliver them as the next chunk to the
            // gesteremaer pipeline.
            auto cursor = Cursor{rate, windowSize, readDelay};

            initializeHighestLatency("Audio", rate, cursor.currentIndex(), cursor.requestedIndex());

            while (!g_exit_requested)
            {
                auto const iterationStartTime = ::mxlGetTime();
                auto const iterationTimeoutNs =
                    (iterationStartTime < cursor.deliveryDeadline()) ? (cursor.deliveryDeadline() - iterationStartTime) : 0ULL;

                mxlWrappedMultiBufferSlice payload;
                auto const ret = ::mxlFlowReaderGetSamples(_reader, cursor.requestedIndex(), windowSize, iterationTimeoutNs, &payload);

                if (ret == MXL_STATUS_OK)
                {
                    updateHighestLatency("Audio", ::mxlIndexToTimestamp(&rate, cursor.requestedIndex()), ::mxlGetTime());

                    auto const payloadLen = windowSize * payload.count * sizeof(float);
                    auto const buffer = ::gst_buffer_new_allocate(nullptr, payloadLen, nullptr);

                    auto const audioMeta = ::gst_buffer_add_audio_meta(buffer, gstPipeline.audioInfo(), windowSize, nullptr);
                    if (!audioMeta)
                    {
                        MXL_ERROR("Error while adding meta to audio buffer.");
                        ::gst_buffer_unref(buffer);
                        continue;
                    }

                    GstAudioBuffer audioBuffer;
                    if (!::gst_audio_buffer_map(&audioBuffer, gstPipeline.audioInfo(), buffer, GST_MAP_WRITE))
                    {
                        MXL_ERROR("Error while mapping audio buffer.");
                        ::gst_buffer_unref(buffer);
                        continue;
                    }

                    for (auto channel = std::size_t{0}; channel < payload.count; ++channel)
                    {
                        auto currentWritePtr = static_cast<std::byte*>(audioBuffer.planes[channel]);
                        auto const readPtr0 = static_cast<std::byte const*>(payload.base.fragments[0].pointer) + channel * payload.stride;
                        auto const readSize0 = payload.base.fragments[0].size;
                        ::memcpy(currentWritePtr, readPtr0, readSize0);
                        currentWritePtr += readSize0;

                        auto const readPtr1 = static_cast<std::byte const*>(payload.base.fragments[1].pointer) + channel * payload.stride;
                        auto const readSize1 = payload.base.fragments[1].size;
                        ::memcpy(currentWritePtr, readPtr1, readSize1);
                    }

                    ::gst_audio_buffer_unmap(&audioBuffer);

                    gstPipeline.pushBuffer(buffer, ::mxlIndexToTimestamp(&rate, cursor.requestedIndex() + windowSize));

                    ::gst_buffer_unref(buffer);

                    cursor.next();
                }
                else if (ret == MXL_ERR_FLOW_INVALID)
                {
                    if (handleInvalidFlow(cursor.requestedIndex()))
                    {
                        // Realign to current index.
                        cursor.realign(iterationStartTime);
                    }
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                {
                    // We are too early somehow, keep trying the same index
                    auto runtimeInfo = ::mxlFlowRuntimeInfo{};
                    (void)::mxlFlowReaderGetRuntimeInfo(_reader, &runtimeInfo);

                    // Please note that it can occasionally happen that the last published index in this report is beyond
                    // the requested index, because the flow has been commited to in between the point in time, when the
                    // call to mxlFlowReaderGetSamples() returned and the flow runtime info was fetched.
                    MXL_TRACE("Failed to get samples at index {}: TOO EARLY. Last published {}", cursor.requestedIndex(), runtimeInfo.headIndex);
                }
                else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
                {
                    // Samples expired. Realign to current index. GStreamer will generate silence for missing samples. Consuming applications
                    // should handle this better by inserting silence with a micro fades to prevent clicks and pops.
                    auto runtimeInfo = ::mxlFlowRuntimeInfo{};
                    (void)::mxlFlowReaderGetRuntimeInfo(_reader, &runtimeInfo);
                    MXL_TRACE("Failed to get samples at index {}: TOO LATE. Last published {}", cursor.requestedIndex(), runtimeInfo.headIndex);

                    cursor.realign(iterationStartTime);
                }
                else
                {
                    // On any other error, verify if the _reader is nullptr (which indicates that the flow may have been invalidated).
                    // In that case, try to recreate it. Otherwise, exit.
                    if (_reader == nullptr)
                    {
                        // Create a new reader
                        auto const flowId = uuids::to_string(_configInfo.common.id);
                        if (auto const ret = ::mxlCreateFlowReader(_instance, flowId.c_str(), "", &_reader); ret != MXL_STATUS_OK)
                        {
                            MXL_TRACE("Failed to reopen sound flow reader with status code {}.", static_cast<int>(ret));
                            // Arbitrary wait time before retrying.
                            std::this_thread::sleep_for(std::chrono::milliseconds{500});
                        }
                        else
                        {
                            MXL_INFO("Reconnected to sound flow: {}.", flowId);
                            // Get the flow config info again
                            if (auto const ret = ::mxlFlowReaderGetConfigInfo(_reader, &_configInfo); ret != MXL_STATUS_OK)
                            {
                                // Something is very wrong. we cannot recover from this.
                                MXL_ERROR("Failed to get flow config info with status code {}. Exiting.", static_cast<int>(ret));
                                return;
                            }
                        }

                        // Realign to current index. GStreamer repeats the last valid frame for missing data; consuming applications
                        // should do the same.
                        if (_reader != nullptr)
                        {
                            MXL_DEBUG("Realigning sound flow: {}.", flowId);
                            cursor.realign(iterationStartTime);
                        }
                    }
                    else
                    {
                        MXL_ERROR("Unexpected error when reading the samples at index {} with status {}. Exiting...",
                            cursor.requestedIndex(),
                            static_cast<int>(ret));
                        return;
                    }
                }
            }
        }

    private:
        /**
         * This default constructor is private, because the only reason why it
         * exists is that it can be used by the publicly accessible constructor
         * to delegate to.
         * This delegation is beneficial, because it implies that the destructor
         * will be invoked when the delegating constructor throws an exception,
         * which is what we need in order to clean up partially established
         * state.
         */
        constexpr MxlReader() noexcept
            : _instance{}
            , _reader{}
            , _configInfo{}
            , _highestLatencyNs{}
        {}

        struct Cursor
        {
            Cursor(mxlRational const& rate, std::uint32_t windowSize, std::int64_t readDelay) noexcept
                : _rate{rate}
                , _windowSize{windowSize}
                , _readDelayGrains{((durationInGrains(readDelay) + _windowSize - 1U) / _windowSize) * _windowSize}
            {
                realign(::mxlGetTime());
            }

            void next() noexcept
            {
                ::mxlSleepUntil(_deliveryDeadline);

                _currentIndex += _windowSize;
                _requestedIndex += _windowSize;
                _deliveryDeadline = getDeliveryDeadline();
            }

            void realign(std::uint64_t timeNow) noexcept
            {
                _currentIndex = ((::mxlTimestampToIndex(&_rate, timeNow) + (_windowSize / 2U)) / _windowSize) * _windowSize;
                _requestedIndex = _currentIndex - _readDelayGrains;
                _deliveryDeadline = getDeliveryDeadline();
            }

            [[nodiscard]]
            constexpr std::uint64_t currentIndex() const noexcept
            {
                return _currentIndex;
            }

            [[nodiscard]]
            constexpr std::uint64_t requestedIndex() const noexcept
            {
                return _requestedIndex;
            }

            [[nodiscard]]
            constexpr std::uint64_t deliveryDeadline() const noexcept
            {
                return _deliveryDeadline;
            }

        private:
            [[nodiscard]]
            std::int64_t durationInGrains(std::int64_t duration) const noexcept
            {
                return ::mxlTimestampToIndex(&_rate, duration);
            }

            [[nodiscard]]
            std::uint64_t getDeliveryDeadline() const noexcept
            {
                return ::mxlIndexToTimestamp(&_rate, _currentIndex + _windowSize);
            }

        private:
            mxlRational _rate;
            std::uint32_t _windowSize;
            std::int64_t _readDelayGrains;
            std::uint64_t _currentIndex;
            std::uint64_t _requestedIndex;
            std::uint64_t _deliveryDeadline;
        };

        void initializeHighestLatency(char const* prefix, mxlRational const& rate, std::uint64_t currentIndex, std::uint64_t requestedIndex)
        {
            auto const latencyNs = ::mxlIndexToTimestamp(&rate, currentIndex) - ::mxlIndexToTimestamp(&rate, requestedIndex);
            _highestLatencyNs = latencyNs;
            MXL_INFO("{} latency initialized to: {} ns", prefix, latencyNs);
        }

        void updateHighestLatency(char const* prefix, std::uint64_t mxlTimestamp, std::uint64_t currentMxlTime)
        {
            auto const latencyNs = currentMxlTime > mxlTimestamp ? currentMxlTime - mxlTimestamp : 0;
            if (latencyNs > _highestLatencyNs)
            {
                _highestLatencyNs = latencyNs;
                MXL_INFO("{} latency increase detected: {} ns", prefix, latencyNs);
            }
        }

        /**
         * Handle flow invalidation by attempting to recreate the flow reader.
         * @param requestedIndex The index that was being requested when the flow became invalid
         * @return true if reconnected
         */
        bool handleInvalidFlow(std::uint64_t requestedIndex)
        {
            bool result = false;

            // The upstream flow writer has been closed or recreated. Try to reopen the flow reader.
            MXL_WARN("Flow became invalid at requested index {}. Attempting to reopen reader.", requestedIndex);

            // Clean up existing reader
            ::mxlReleaseFlowReader(_instance, _reader);
            _reader = nullptr;

            // Create a new reader
            auto const flowId = uuids::to_string(_configInfo.common.id);
            if (auto const ret = ::mxlCreateFlowReader(_instance, flowId.c_str(), "", &_reader); ret != MXL_STATUS_OK)
            {
                MXL_TRACE("Failed to reopen sound flow reader with status code {}.", static_cast<int>(ret));
                result = false;
            }
            else
            {
                MXL_INFO("Reconnected to sound flow: {}.", flowId);

                // Get the flow config info again
                if (auto const ret = mxlFlowReaderGetConfigInfo(_reader, &_configInfo); ret != MXL_STATUS_OK)
                {
                    // Inconsistant state. close the reader and try later.
                    MXL_ERROR("Failed to get flow config info with status code {}.", static_cast<int>(ret));
                    ::mxlReleaseFlowReader(_instance, _reader);
                    _reader = nullptr;
                    result = false;
                }
                else
                {
                    result = true;
                }
            }
            return result;
        }

    private:
        mxlInstance _instance;
        mxlFlowReader _reader;
        mxlFlowConfigInfo _configInfo;
        std::uint64_t _highestLatencyNs;
    };

    std::string readFlowDescriptor(std::string const& domain, std::string const& flowID)
    {
        auto const opts = "{}";
        auto instance = mxlCreateInstance(domain.c_str(), opts);
        if (instance == nullptr)
        {
            throw std::runtime_error{"Failed to create MXL instance."};
        }

        char fourKBuffer[4096];
        auto fourKBufferSize = sizeof(fourKBuffer);
        auto requiredBufferSize = fourKBufferSize;

        if (mxlGetFlowDef(instance, flowID.c_str(), fourKBuffer, &requiredBufferSize) != MXL_STATUS_OK)
        {
            throw std::runtime_error{"Failed to get flow definition for flow id " + flowID};
        }

        return std::string{fourKBuffer, requiredBufferSize - 1};
    }

    int real_main(int argc, char** argv, void*)
    {
        std::signal(SIGINT, &signal_handler);
        std::signal(SIGTERM, &signal_handler);

        auto app = CLI::App{"mxl-gst-sink"};

        auto videoFlowID = std::string{};
        app.add_option("-v, --video-flow-id", videoFlowID, "The video flow ID");

        auto audioFlowID = std::string{};
        app.add_option("-a, --audio-flow-id", audioFlowID, "The audio flow ID");

        auto domain = std::string{};
        auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory.");
        domainOpt->required(true);
        domainOpt->check(CLI::ExistingDirectory);

        auto listenChannels = std::vector<std::size_t>{};
        auto listenChanOpt = app.add_option("-l, --listen-channels", listenChannels, "Audio channels to listen.");
        listenChanOpt->default_val(std::vector<std::size_t>{0, 1});

        auto readDelay = std::int64_t{};
        auto readDelayOpt = app.add_option(
            "--read-delay", readDelay, "How far in the past/future to read (in nanoseconds). A positive values means you are delaying the read.");
        readDelayOpt->default_val(40'000'000);

        auto playbackDelay = std::int64_t{};
        auto playbackDelayOpt = app.add_option(
            "--playback-delay", playbackDelay, "The time in nanoseconds, by which to delay playback of audio and/or video.");
        playbackDelayOpt->default_val(0);

        auto audioVideoOffset = std::int64_t{};
        auto audioVideoOffsetOpt = app.add_option("--av-delay",
            audioVideoOffset,
            "The time in nanoseconds, by which to delay the audio relative to video. A positive value means you are delaying audio, a negative value "
            "means you are delaying video.");
        audioVideoOffsetOpt->default_val(0);

        CLI11_PARSE(app, argc, argv);

        ::gst_init(nullptr, nullptr);

        auto threads = std::vector<std::thread>{};

        if (!videoFlowID.empty())
        {
            threads.emplace_back(
                [&]()
                {
                    try
                    {
                        auto reader = MxlReader{domain, videoFlowID};

                        auto const flowDescriptor = readFlowDescriptor(domain, videoFlowID);
                        auto const flowNmos = json_utils::parseBuffer(flowDescriptor);

                        if (json_utils::getField<std::string>(flowNmos, "interlace_mode") != "progressive")
                        {
                            throw std::invalid_argument{"This application does not support interlaced flows."};
                        }

                        auto const grainRate = json_utils::getRational(flowNmos, "grain_rate");
                        auto const frameWidth = static_cast<std::uint32_t>(json_utils::getField<double>(flowNmos, "frame_width"));
                        auto const frameHeight = static_cast<std::uint32_t>(json_utils::getField<double>(flowNmos, "frame_height"));

                        auto videoConfig = VideoPipelineConfig{
                            .frameRate = grainRate,
                            .frameWidth = static_cast<std::uint64_t>(frameWidth),
                            .frameHeight = static_cast<std::uint64_t>(frameHeight),
                            .offset = playbackDelay + ((audioVideoOffset < 0) ? -audioVideoOffset : 0LL),
                        };

                        auto pipeline = VideoPipeline{videoConfig};
                        reader.run(pipeline, readDelay);

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

        if (!audioFlowID.empty())
        {
            threads.emplace_back(
                [&]()
                {
                    try
                    {
                        auto reader = MxlReader{domain, audioFlowID};
                        auto const flowDescriptor = readFlowDescriptor(domain, audioFlowID);
                        auto flowNmos = json_utils::parseBuffer(flowDescriptor);

                        auto grainRate = json_utils::getRational(flowNmos, "sample_rate");
                        auto channelCount = static_cast<std::uint32_t>(json_utils::getField<double>(flowNmos, "channel_count"));

                        auto audioConfig = AudioPipelineConfig{
                            .sampleRate = grainRate,
                            .channelCount = channelCount,
                            .offset = playbackDelay + ((audioVideoOffset > 0) ? audioVideoOffset : 0LL),
                            .speakerChannels = listenChannels,
                        };

                        auto pipeline = AudioPipeline{audioConfig};
                        reader.run(pipeline, readDelay);

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

}

int main(int argc, char* argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
    // macOS needs an NSApp event loop.  This gst function sets it up.
    return ::gst_macos_main((GstMainFunc)real_main, argc, argv, nullptr);
#else
    return real_main(argc, argv, nullptr);
#endif
}
