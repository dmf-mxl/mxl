// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <CLI/CLI.hpp>
#include <glib-object.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstelement.h>
#include <gst/gstelementfactory.h>
#include <gst/gstformat.h>
#include <gst/gstmemory.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstpipeline.h>
#include <gst/gstutils.h>
#include <gst/gstvalue.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "../../lib/src/internal/FlowParser.hpp"
#include "../../lib/src/internal/Logging.hpp"
#include "../../lib/src/internal/PathUtils.hpp"

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

std::sig_atomic_t volatile g_exit_requested = 0;

void signal_handler(int)
{
    g_exit_requested = 1;
}

struct GstreamerAudioPipelineConfig
{
    mxlRational rate;
    std::size_t channelCount;
};

struct GstreamerPipelineConfig
{
    uint64_t frame_width;
    uint64_t frame_height;
    mxlRational frame_rate;
    std::optional<GstreamerAudioPipelineConfig> audio_config;
};

/// The returned caps have to be freed with gst_caps_unref().
GstCaps* gstCapsFromAudioConfig(GstreamerAudioPipelineConfig const& config)
{
    guint64 channelMask{gst_audio_channel_get_fallback_mask(config.channelCount)};
    return gst_caps_new_simple("audio/x-raw",
        "format",
        G_TYPE_STRING,
        "F32LE",
        "rate",
        G_TYPE_INT,
        config.rate.numerator,
        "channels",
        G_TYPE_INT,
        config.channelCount,
        "layout",
        G_TYPE_STRING,
        "non-interleaved",
        "channel-mask",
        GST_TYPE_BITMASK,
        channelMask,
        nullptr);
}

/// Encapsulation of the Gstreamer pipeline used to play data received from MXL.
///
/// The current implementation assumes that the video is always present and that the audio is optional.
class GstreamerPipeline final
{
public:
    GstreamerPipeline(GstreamerPipelineConfig const& config)
    {
        gst_init(nullptr, nullptr);

        _pipeline = gst_pipeline_new("test-pipeline");
        if (!_pipeline)
        {
            throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
        }

        // Create the elements
        _videoAppsrc = gst_element_factory_make("appsrc", "video_source");
        if (!_videoAppsrc)
        {
            throw std::runtime_error("Gstreamer: 'appsrc' for video could not be created.");
        }
        gst_bin_add(GST_BIN(_pipeline), _videoAppsrc);
        // Configure appsrc
        g_object_set(G_OBJECT(_videoAppsrc),
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
            "format",
            GST_FORMAT_TIME,
            nullptr);

        _videoconvert = gst_element_factory_make("videoconvert", "video_convert");
        if (!_videoconvert)
        {
            throw std::runtime_error("Gstreamer: 'videoconvert' could not be created.");
        }
        gst_bin_add(GST_BIN(_pipeline), _videoconvert);

        _videoscale = gst_element_factory_make("videoscale", "video_scale");
        if (!_videoscale)
        {
            throw std::runtime_error("Gstreamer: 'videoscale' could not be created.");
        }
        gst_bin_add(GST_BIN(_pipeline), _videoscale);

        _videoqueue = gst_element_factory_make("queue", "video_queue");
        if (!_videoqueue)
        {
            throw std::runtime_error("Gstreamer: 'queue' for video could not be created.");
        }
        gst_bin_add(GST_BIN(_pipeline), _videoqueue);

        _autovideosink = gst_element_factory_make("autovideosink", "video_sink");
        if (!_autovideosink)
        {
            throw std::runtime_error("Gstreamer: 'autovideosink' could not be created.");
        }
        gst_bin_add(GST_BIN(_pipeline), _autovideosink);

        if (config.audio_config)
        {
            _audioAppsrc = gst_element_factory_make("appsrc", "audio_source");
            if (!_audioAppsrc)
            {
                throw std::runtime_error("Gstreamer: 'appsrc' for audio could not be created.");
            }
            gst_bin_add(GST_BIN(_pipeline), _audioAppsrc);
            if (config.audio_config->rate.denominator != 1)
            {
                throw std::runtime_error("Audio rate denominator must be 1.");
            }
            _audioCaps = gstCapsFromAudioConfig(*config.audio_config);
            g_object_set(G_OBJECT(_audioAppsrc), "caps", _audioCaps, "format", GST_FORMAT_TIME, nullptr);

            _audioconvert = gst_element_factory_make("audioconvert", "audio_convert");
            if (!_audioconvert)
            {
                throw std::runtime_error("Gstreamer: 'audioconvert' could not be created.");
            }
            gst_bin_add(GST_BIN(_pipeline), _audioconvert);

            _audioqueue = gst_element_factory_make("queue", "audio_queue");
            if (!_audioqueue)
            {
                throw std::runtime_error("Gstreamer: 'queue' for audio could not be created.");
            }
            gst_bin_add(GST_BIN(_pipeline), _audioqueue);

            _autoaudiosink = gst_element_factory_make("autoaudiosink", "audio_sink");
            if (!_autoaudiosink)
            {
                throw std::runtime_error("Gstreamer: 'autoaudiosink' could not be created.");
            }
            gst_bin_add(GST_BIN(_pipeline), _autoaudiosink);
        }

        // Build the pipeline
        if (gst_element_link_many(_videoAppsrc, _videoconvert, _videoscale, _videoqueue, _autovideosink, nullptr) != TRUE)
        {
            throw std::runtime_error("Gstreamer: Video elements could not be linked.");
        }
        if (config.audio_config)
        {
            if (gst_element_link_many(_audioAppsrc, _audioconvert, _audioqueue, _autoaudiosink, nullptr) != TRUE)
            {
                throw std::runtime_error("Gstreamer: Audio elements could not be linked.");
            }
        }

        _audioConfig = config.audio_config;
    }

    ~GstreamerPipeline()
    {
        if (_audioCaps)
        {
            gst_caps_unref(_audioCaps);
        }
        if (_pipeline)
        {
            gst_element_set_state(_pipeline, GST_STATE_NULL);
            gst_object_unref(_pipeline);
        }
        // The pipeline owns all the elements, we do not need to free them individually.

        gst_deinit();
    }

    void start(std::uint64_t baseTimestamp)
    {
        // Start playing
        gst_element_set_state(_pipeline, GST_STATE_PLAYING);
        _baseTimestamp = baseTimestamp;
    }

    void pushVideoSample(uint8_t* payload, size_t payloadLen, std::uint64_t mxlTimestamp, std::uint64_t currentMxlTime)
    {
        updateHighestLatency(mxlTimestamp, currentMxlTime);

        GstBuffer* gstBuffer{gst_buffer_new_allocate(nullptr, payloadLen, nullptr)};

        GstMapInfo map;
        gst_buffer_map(gstBuffer, &map, GST_MAP_WRITE);
        ::memcpy(map.data, payload, payloadLen);
        gst_buffer_unmap(gstBuffer, &map);
        GST_BUFFER_PTS(gstBuffer) = mxlTimestamp - _baseTimestamp + _highestLatencyNs;
        MXL_DEBUG("Pushing video sample with PTS: {} ns", GST_BUFFER_PTS(gstBuffer));

        int ret;
        g_signal_emit_by_name(_videoAppsrc, "push-buffer", gstBuffer, &ret);
        if (ret != GST_FLOW_OK)
        {
            MXL_ERROR("Error pushing buffer to video appsrc");
        }

        gst_buffer_unref(gstBuffer);
    }

    void pushAudioSamples(mxlWrappedMultiBufferSlice const& payload, std::uint64_t firstSampleMxlTimestamp, std::uint64_t lastSampleMxlTimestamp,
        std::uint64_t currentMxlTime)
    {
        updateHighestLatency(lastSampleMxlTimestamp, currentMxlTime);

        auto const oneChannelBufferSize{payload.base.fragments[0].size + payload.base.fragments[1].size};
        gsize payloadLen{oneChannelBufferSize * payload.count};
        GstBuffer* gstBuffer{gst_buffer_new_allocate(nullptr, payloadLen, nullptr)};

        // This is as inefficient as you get it, but hey, first iteration...
        GstAudioInfo audioInfo;
        if (!gst_audio_info_from_caps(&audioInfo, _audioCaps))
        {
            MXL_ERROR("Error while creating audio info from caps.");
            gst_buffer_unref(gstBuffer);
            return;
        };
        auto const numOfSamples{oneChannelBufferSize / sizeof(float)};
        GstAudioMeta* audioMeta{gst_buffer_add_audio_meta(gstBuffer, &audioInfo, numOfSamples, nullptr)};
        if (!audioMeta)
        {
            MXL_ERROR("Error while adding meta to audio buffer.");
            gst_buffer_unref(gstBuffer);
            return;
        }
        GstAudioBuffer audioBuffer;
        if (!gst_audio_buffer_map(&audioBuffer, &audioInfo, gstBuffer, GST_MAP_WRITE))
        {
            MXL_ERROR("Error while mapping audio buffer.");
            gst_buffer_unref(gstBuffer);
            return;
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
        GST_BUFFER_PTS(gstBuffer) = firstSampleMxlTimestamp - _baseTimestamp + _highestLatencyNs;
        MXL_DEBUG("Pushing {} audio samples with PTS: {} ns", oneChannelBufferSize / 4, GST_BUFFER_PTS(gstBuffer));

        int ret;
        g_signal_emit_by_name(_audioAppsrc, "push-buffer", gstBuffer, &ret);
        if (ret != GST_FLOW_OK)
        {
            MXL_ERROR("Error pushing buffer to video appsrc");
        }

        gst_buffer_unref(gstBuffer);
    }

private:
    GstElement* _videoAppsrc{nullptr};
    GstElement* _videoconvert{nullptr};
    GstElement* _videoscale{nullptr};
    GstElement* _videoqueue{nullptr};
    GstElement* _autovideosink{nullptr};
    GstElement* _audioAppsrc{nullptr};
    GstElement* _audioconvert{nullptr};
    GstElement* _audioqueue{nullptr};
    GstElement* _autoaudiosink{nullptr};
    GstElement* _pipeline{nullptr};
    /// Indicates the MXL timestamp at the moment the pipeline got started. Is used to calculate the PTS of the buffers based on the pipeline's
    /// running time.
    std::uint64_t _baseTimestamp{0};
    /// Indicates the latency in ns at which we are getting the data. We adjust PTS values based on this information to ensure fluent playback.
    std::uint64_t _highestLatencyNs{0};
    /// The following information is needed when passing audio samples in non-interleaved formate.
    std::optional<GstreamerAudioPipelineConfig> _audioConfig{std::nullopt};
    /// We use the caps to initialize the audio metadata when pushing audio samples. There are probably more efficient ways to do this, but this
    /// should be good enough for the first iteration.
    GstCaps* _audioCaps{nullptr};

    void updateHighestLatency(std::uint64_t mxlTimestamp, std::uint64_t currentMxlTime)
    {
        auto const latencyNs = currentMxlTime > mxlTimestamp ? currentMxlTime - mxlTimestamp : 0;
        if (latencyNs > _highestLatencyNs)
        {
            _highestLatencyNs = latencyNs;
            MXL_INFO("Latency increase detected: {} ns", latencyNs);
        }
    }
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

GstreamerPipelineConfig prepare_gstreamer_config(std::string const& domain, std::string const& flowID, std::optional<std::string> const& audioFlowID)
{
    std::string flow_descriptor{read_flow_descriptor(domain, flowID)};
    mxl::lib::FlowParser descriptor_parser{flow_descriptor};

    std::optional<GstreamerAudioPipelineConfig> audio_config;
    if (audioFlowID)
    {
        std::string audio_flow_descriptor{read_flow_descriptor(domain, *audioFlowID)};
        mxl::lib::FlowParser audio_descriptor_parser{audio_flow_descriptor};
        audio_config = GstreamerAudioPipelineConfig{
            .rate = audio_descriptor_parser.getGrainRate(), .channelCount = audio_descriptor_parser.getChannelCount()};
    }
    else
    {
        audio_config = std::nullopt;
    }

    return GstreamerPipelineConfig{.frame_width = static_cast<uint64_t>(descriptor_parser.get<double>("frame_width")),
        .frame_height = static_cast<uint64_t>(descriptor_parser.get<double>("frame_height")),
        .frame_rate = descriptor_parser.getGrainRate(),
        .audio_config = audio_config};
}

int real_main(int argc, char** argv, void*)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-gst-videosink");

    std::string videoFlowID;
    auto videoFlowIDOpt = app.add_option("-v, --video-flow-id", videoFlowID, "The video flow ID");
    videoFlowIDOpt->required(true);

    std::optional<std::string> audioFlowID;
    auto audioFlowIDOpt = app.add_option("-a, --audio-flow-id", audioFlowID, "The audio flow ID");
    audioFlowIDOpt->required(false);

    std::string domain;
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    std::optional<std::uint64_t> readTimeoutNs;
    auto readTimeoutOpt = app.add_option("-t,--timeout", readTimeoutNs, "The read timeout in ns, frame interval + 1 ms used if not specified");
    readTimeoutOpt->required(false);
    readTimeoutOpt->default_val(std::nullopt);

    CLI11_PARSE(app, argc, argv);

    GstreamerPipelineConfig gst_config{prepare_gstreamer_config(domain, videoFlowID, audioFlowID)};
    GstreamerPipeline gst_pipeline(gst_config);

    int exit_status = EXIT_SUCCESS;
    mxlStatus ret;

    mxlRational videoRate;
    std::uint64_t grain_index = 0;

    mxlFlowReader videoReader{nullptr};
    mxlFlowReader audioReader{nullptr};
    auto instance = mxlCreateInstance(domain.c_str(), "");
    if (instance == nullptr)
    {
        MXL_ERROR("Failed to create MXL instance");
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    ret = mxlCreateFlowReader(instance, videoFlowID.c_str(), "", &videoReader);
    if (ret != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create video flow reader with status '{}'", static_cast<int>(ret));
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }
    mxlFlowInfo videoFlowInfo;
    ret = mxlFlowReaderGetInfo(videoReader, &videoFlowInfo);
    if (ret != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to get video flow info with status '{}'", static_cast<int>(ret));
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    mxlFlowInfo audioFlowInfo;
    if (audioFlowID)
    {
        ret = mxlCreateFlowReader(instance, audioFlowID->c_str(), "", &audioReader);
        if (ret != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create audio flow reader with status '{}'", static_cast<int>(ret));
            exit_status = EXIT_FAILURE;
            goto mxl_cleanup;
        }
        ret = mxlFlowReaderGetInfo(audioReader, &audioFlowInfo);
        if (ret != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get audio flow info with status '{}'", static_cast<int>(ret));
            exit_status = EXIT_FAILURE;
            goto mxl_cleanup;
        }
    }

    videoRate = videoFlowInfo.discrete.grainRate;
    std::uint64_t actualReadTimeoutNs;
    if (readTimeoutNs)
    {
        actualReadTimeoutNs = *readTimeoutNs;
    }
    else
    {
        actualReadTimeoutNs = static_cast<std::uint64_t>(1.0 * videoRate.denominator * (1'000'000'000.0 / videoRate.numerator));
        actualReadTimeoutNs += 1'000'000ULL; // allow some margin.
    }
    gst_pipeline.start(mxlGetTime());

    grain_index = mxlGetCurrentIndex(&videoFlowInfo.discrete.grainRate);

    while (!g_exit_requested)
    {
        mxlGrainInfo grain_info;
        uint8_t* payload;
        auto ret = mxlFlowReaderGetGrain(videoReader, grain_index, actualReadTimeoutNs, &grain_info, &payload);
        if (ret != MXL_STATUS_OK)
        {
            // Missed a grain. resync.
            MXL_ERROR("Missed grain {}, err : {}", grain_index, (int)ret);
            grain_index = mxlGetCurrentIndex(&videoFlowInfo.discrete.grainRate);
            continue;
        }
        else if (grain_info.commitedSize != grain_info.grainSize)
        {
            // we don't need partial grains. wait for the grain to be complete.
            continue;
        }
        std::uint64_t videoTimestamp = mxlIndexToTimestamp(&videoFlowInfo.discrete.grainRate, grain_index);
        gst_pipeline.pushVideoSample(payload, grain_info.grainSize, videoTimestamp, mxlGetTime());

        // Currently there is neither blocking read for audio nor other way how to make sure both audio and video data are available. Here we just
        // hackishly assume that audio is available earlier than video, and thus it is safe to read as much audio as we already have video.
        if (audioReader)
        {
            std::uint64_t previousVideoTimestamp = mxlIndexToTimestamp(&videoFlowInfo.discrete.grainRate, grain_index - 1);
            std::uint64_t lastReadAudioIndex = mxlTimestampToIndex(&audioFlowInfo.continuous.sampleRate, previousVideoTimestamp);
            std::uint64_t currentAudioIndex = mxlTimestampToIndex(&audioFlowInfo.continuous.sampleRate, videoTimestamp);
            std::uint64_t samplesToRead = currentAudioIndex - lastReadAudioIndex;
            std::uint64_t firstAudioSampleTimestamp = mxlIndexToTimestamp(&audioFlowInfo.continuous.sampleRate, lastReadAudioIndex + 1);
            std::uint64_t lastAudioSampleTimestamp = mxlIndexToTimestamp(&audioFlowInfo.continuous.sampleRate, currentAudioIndex);

            mxlWrappedMultiBufferSlice audioPayload;
            auto ret = mxlFlowReaderGetSamples(audioReader, currentAudioIndex, samplesToRead, &audioPayload);
            if (ret != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to read audio samples for video grain {} with status '{}'", grain_index, static_cast<int>(ret));
                if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                {
                    MXL_ERROR("Successful video timestamp was {}, failing audio timestamp was {}. Does audio precede video?",
                        videoTimestamp,
                        lastAudioSampleTimestamp);
                }
                // Fake missing data with silence. This is an error condition, so it is OK to allocate / free data here.
                auto const bufferLen = samplesToRead * 4;
                audioPayload.count = gst_config.audio_config->channelCount;
                audioPayload.stride = 0;
                auto payloadPtr = std::malloc(bufferLen);
                std::memset(payloadPtr, 0, bufferLen);
                audioPayload.base.fragments[0].pointer = payloadPtr;
                audioPayload.base.fragments[0].size = bufferLen;
                audioPayload.base.fragments[1].pointer = nullptr;
                audioPayload.base.fragments[1].size = 0;
                gst_pipeline.pushAudioSamples(audioPayload, firstAudioSampleTimestamp, lastAudioSampleTimestamp, mxlGetTime());
                std::free(payloadPtr);
            }
            else
            {
                gst_pipeline.pushAudioSamples(audioPayload, firstAudioSampleTimestamp, lastAudioSampleTimestamp, mxlGetTime());
            }
        }

        grain_index++;
    }

mxl_cleanup:
    if (instance != nullptr)
    {
        // clean-up mxl objects
        if (videoReader)
        {
            mxlReleaseFlowReader(instance, videoReader);
        }
        if (audioReader)
        {
            mxlReleaseFlowReader(instance, audioReader);
        }
        mxlDestroyInstance(instance);
    }

    return exit_status;
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
