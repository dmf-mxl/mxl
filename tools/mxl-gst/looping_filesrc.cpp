// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <climits>
#include <csignal>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <glib-object.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstobject.h>
#include <gst/gstvalue.h>
#include <picojson/picojson.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "utils.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl/rational.h"

namespace fs = std::filesystem;

std::sig_atomic_t volatile g_exit_requested = 0;

void signal_handler(int in_signal)
{
    switch (in_signal)
    {
        case SIGINT:  MXL_INFO("Received SIGINT, exiting..."); break;
        case SIGTERM: MXL_INFO("Received SIGTERM, exiting..."); break;
        default:      MXL_INFO("Received signal {}, exiting...", in_signal); break;
    }
    g_exit_requested = 1;
}

class LoopingFilePlayer
{
public:
    constexpr static mxlRational defaultAudioGrainRate = mxlRational{48000, 1};

    static void cb_pad_added(GstElement* element, GstPad* pad, gpointer data)
    {
        (void)element;

        auto* self = static_cast<LoopingFilePlayer*>(data);
        GstCaps* caps = gst_pad_query_caps(pad, nullptr);
        gchar const* name = gst_structure_get_name(gst_caps_get_structure(caps, 0));

        bool padDiscarded = true;

        if (!self->appSinkVideo && g_str_has_prefix(name, "video/")) // video pad
        {
            padDiscarded = false;

            GstElement* queue = gst_element_factory_make("queue", nullptr);
            GstElement* videorate = gst_element_factory_make("videorate", nullptr);
            GstElement* videoconvert = gst_element_factory_make("videoconvert", nullptr);
            self->appSinkVideo = gst_element_factory_make("appsink", "appSinkVideo");

            g_object_set(G_OBJECT(self->appSinkVideo),
                "caps",
                gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "v210", "colorimetry", G_TYPE_STRING, "bt709", nullptr),
                "max-buffers",
                20,
                "emit-signals",
                FALSE,
                "sync",
                TRUE,
                nullptr);

            gst_bin_add_many(GST_BIN(self->pipeline), queue, videorate, videoconvert, self->appSinkVideo, nullptr);

            if (!gst_element_link_many(queue, videorate, videoconvert, self->appSinkVideo, nullptr))
            {
                MXL_ERROR("Failed to link elements of video pipeline.");
                return;
            }

            for (GstElement* e : {queue, videorate, videoconvert, self->appSinkVideo})
            {
                gst_element_sync_state_with_parent(e);
            }

            GstPad* sinkpad = gst_element_get_static_pad(queue, "sink");
            gst_pad_link(pad, sinkpad);

            gst_object_unref(sinkpad);
        }
        else if (!self->appSinkAudio && g_str_has_prefix(name, "audio/")) // audio pad
        {
            padDiscarded = false;

            GstElement* queue = gst_element_factory_make("queue", nullptr);
            GstElement* audioconvert = gst_element_factory_make("audioconvert", nullptr);
            self->appSinkAudio = gst_element_factory_make("appsink", "appSinkAudio");

            g_object_set(G_OBJECT(self->appSinkAudio),
                "caps",
                gst_caps_new_simple("audio/x-raw",
                    "format",
                    G_TYPE_STRING,
                    "F32LE",
                    "rate",
                    G_TYPE_INT,
                    defaultAudioGrainRate.numerator,
                    "layout",
                    G_TYPE_STRING,
                    "non-interleaved",
                    nullptr),
                "emit-signals",
                FALSE,
                "sync",
                TRUE,
                nullptr);

            gst_bin_add_many(GST_BIN(self->pipeline), queue, audioconvert, self->appSinkAudio, nullptr);

            if (!gst_element_link_many(queue, audioconvert, self->appSinkAudio, nullptr))
            {
                MXL_ERROR("Failed to link elements of audio pipeline.");
                return;
            }

            for (GstElement* e : {queue, audioconvert, self->appSinkAudio})
            {
                gst_element_sync_state_with_parent(e);
            }

            GstPad* sinkpad = gst_element_get_static_pad(queue, "sink");
            gst_pad_link(pad, sinkpad);

            gst_object_unref(sinkpad);
        }

        MXL_INFO("Decodebin pad: {} {}", name, padDiscarded ? "(Discarded)" : "");

        gst_caps_unref(caps);
    }

    LoopingFilePlayer(std::string in_domain)
        : domain(std::move(in_domain))
    {
        // Create the MXL domain directory if it doesn't exist
        if (!fs::exists(domain))
        {
            try
            {
                fs::create_directories(domain);
                MXL_DEBUG("Created MXL domain directory: {}", domain);
            }
            catch (fs::filesystem_error const& e)
            {
                MXL_ERROR("Error creating domain directory: {}", e.what());
                throw;
            }
        }

        // Create the MXL SDK instance
        mxlInstance = mxlCreateInstance(domain.c_str(), nullptr);
        if (!mxlInstance)
        {
            throw std::runtime_error("Failed to create MXL instance");
        }
    }

    ~LoopingFilePlayer()
    {
        // Join video threads if they were created
        if (videoThreadPtr && videoThreadPtr->joinable())
        {
            videoThreadPtr->join();
        }

        // Join audio threads if they were created
        if (audioThreadPtr && audioThreadPtr->joinable())
        {
            audioThreadPtr->join();
        }

        if (pipeline)
        {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }

        if (appSinkAudio && GST_OBJECT_REFCOUNT_VALUE(appSinkAudio))
        {
            gst_object_unref(appSinkAudio);
        }

        if (appSinkVideo && GST_OBJECT_REFCOUNT_VALUE(appSinkVideo))
        {
            gst_object_unref(appSinkVideo);
        }

        if (mxlInstance)
        {
            if (flowWriterVideo)
            {
                mxlReleaseFlowWriter(mxlInstance, flowWriterVideo);
            }

            if (flowWriterAudio)
            {
                mxlReleaseFlowWriter(mxlInstance, flowWriterAudio);
                auto id = uuids::to_string(audioFlowId);
                mxlDestroyFlow(mxlInstance, id.c_str());
            }

            mxlDestroyInstance(mxlInstance);
        }
    }

    bool open(std::string const& in_uri)
    {
        uri = in_uri;
        MXL_DEBUG("Opening URI: {}", uri);

        // Create gstreamer pipeline
        pipeline = gst_pipeline_new("media-pipeline");

        auto src = gst_element_factory_make("looping_filesrc", "src");
        auto decode = gst_element_factory_make("decodebin", "decode");

        if (!pipeline || !src || !decode)
        {
            MXL_ERROR("Failed to create pipeline/looping_filesrc/decodebin GStreamer elements.");
            return false;
        }

        g_object_set(src, "location", in_uri.c_str(), nullptr);

        gst_bin_add_many(GST_BIN(pipeline), src, decode, nullptr);

        g_signal_connect(decode, "pad-added", G_CALLBACK(cb_pad_added), this);

        if (!gst_element_link(src, decode))
        {
            MXL_ERROR("Failed to link base elements.");
            return false;
        }

        if (auto clock = gst_pipeline_get_clock(GST_PIPELINE(pipeline)); clock)
        {
            g_object_set(G_OBJECT(clock), "clock-type", GST_CLOCK_TYPE_TAI, nullptr);
            gst_object_unref(clock);
        }

        gst_element_set_state(pipeline, GST_STATE_PAUSED);

        auto bus = gst_element_get_bus(pipeline);

        bool negotiated = false;
        while (!negotiated)
        {
            GstMessage* in_msg = gst_bus_timed_pop_filtered(
                bus, GST_CLOCK_TIME_NONE, static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

            if (!in_msg)
            {
                continue;
            }

            switch (GST_MESSAGE_TYPE(in_msg))
            {
                case GST_MESSAGE_ASYNC_DONE: negotiated = true; break;
                case GST_MESSAGE_ERROR:
                {
                    GError* err;
                    gchar* debug;
                    gst_message_parse_error(in_msg, &err, &debug);
                    MXL_ERROR("Pipeline error: {} ", err->message);
                    g_error_free(err);
                    g_free(debug);
                    break;
                }
                default: break;
            }
            gst_message_unref(in_msg);
        }
        gst_object_unref(bus);

        if (!appSinkVideo && !appSinkAudio)
        {
            MXL_ERROR("No audio and video appsink found");
            return false;
        }

        if (appSinkVideo != nullptr)
        {
            MXL_DEBUG("Creating MXL flow for video...");

            // Get negotiated caps from appsink's pad
            GstPad* pad = gst_element_get_static_pad(appSinkVideo, "sink");
            GstCaps* caps = gst_pad_get_current_caps(pad);
            int width = 0, height = 0, fps_n = 0, fps_d = 1;
            gchar const* interlace_mode = nullptr;
            gchar const* colorimetry = nullptr;

            if (caps)
            {
                GstStructure* s = gst_caps_get_structure(caps, 0);
                interlace_mode = gst_structure_get_string(s, "interlace-mode");
                colorimetry = gst_structure_get_string(s, "colorimetry");

                gst_structure_get_int(s, "width", &width);
                gst_structure_get_int(s, "height", &height);

                if (width <= 0 || height <= 0)
                {
                    MXL_ERROR("Invalid width or height in caps");
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (!gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d))
                {
                    MXL_ERROR("Failed to get framerate from caps");
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (fps_n <= 0 || fps_d <= 0)
                {
                    MXL_ERROR("Invalid framerate in caps {}/{}", fps_n, fps_d);
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }
                else if (fps_n == 0 && fps_d == 1)
                {
                    MXL_ERROR("Invalid framerate in caps {}/{}.  This potentially signals that the video stream is VFR (variable frame rate) which "
                              "is unsupported by this application.",
                        fps_n,
                        fps_d);
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (!interlace_mode)
                {
                    MXL_ERROR("Failed to get interlace mode from caps. Assuming progressive.");
                }

                if (!g_str_equal(interlace_mode, "progressive"))
                {
                    // TODO : Handle interlaced video
                    MXL_ERROR("Unsupported interlace mode.  Interpreting as progressive.");
                }

                // This assumes square pixels, bt709, sdr.  TODO read from caps.
                gst_caps_unref(caps);
            }
            else
            {
                MXL_ERROR("Failed to get caps from appSinkVideo pad");
                gst_object_unref(pad);
                return false;
            }

            gst_object_unref(pad);

            std::string flowDef;
            videoGrainRate = mxlRational{fps_n, fps_d};
            videoFlowId = createVideoFlowJson(uri, width, height, videoGrainRate, true, colorimetry, flowDef);

            mxlFlowConfigInfo configInfo;
            bool flowCreated = false;
            auto res = mxlCreateFlowWriter(mxlInstance, flowDef.c_str(), nullptr, &flowWriterVideo, &configInfo, &flowCreated);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow writer: {}", (int)res);
                return false;
            }

            if (!flowCreated)
            {
                MXL_WARN("Reusing existing flow.");
            }

            MXL_INFO("Video flow : {}", uuids::to_string(videoFlowId));
        }

        if (appSinkAudio != nullptr)
        {
            MXL_DEBUG("Creating MXL flow for audio...");

            // Get negotiated caps from appsink's pad
            GstPad* pad = gst_element_get_static_pad(appSinkAudio, "sink");
            GstCaps* caps = gst_pad_get_current_caps(pad);
            GstAudioInfo* audio_info = gst_audio_info_new_from_caps(caps);
            gst_caps_unref(caps);

            std::uint32_t channels = 0, rate = 0, depth = 0;
            char const* format = nullptr;

            if (audio_info)
            {
                channels = GST_AUDIO_INFO_CHANNELS(audio_info);
                rate = GST_AUDIO_INFO_RATE(audio_info);
                depth = GST_AUDIO_INFO_DEPTH(audio_info);
                format = gst_audio_format_to_string(GST_AUDIO_INFO_FORMAT(audio_info));

                if (channels == 0)
                {
                    MXL_ERROR("Invalid channel count");
                    gst_object_unref(pad);
                    gst_audio_info_free(audio_info);
                    return false;
                }

                if (rate == 0)
                {
                    MXL_ERROR("Invalid sample rate");
                    gst_object_unref(pad);
                    gst_audio_info_free(audio_info);
                    return false;
                }

                if (depth == 0)
                {
                    MXL_ERROR("Invalid depth");
                    gst_object_unref(pad);
                    gst_audio_info_free(audio_info);
                    return false;
                }

                if (!format)
                {
                    MXL_ERROR("Failed to get format from caps.");
                    gst_object_unref(pad);
                    gst_audio_info_free(audio_info);
                    return false;
                }

                gst_audio_info_free(audio_info);
            }
            else
            {
                MXL_ERROR("Failed to get audio info from appSinkAudio pad");
                gst_object_unref(pad);
                return false;
            }

            gst_object_unref(pad);

            std::string flowDef;
            audioGrainRate = mxlRational{rate, 1};
            audioChannels = channels;
            audioFlowId = createAudioFlowJson(uri, audioGrainRate, channels, depth, format, flowDef);

            // The pipeline is PAUSED and the appSinkAudio should have received its preroll buffer.
            // We can try to pull this preroll sample to inspect the first decoded audio buffer
            // Default to 10ms worth of samples
            std::uint32_t batchSize = audioGrainRate.numerator / (100U * audioGrainRate.denominator);
            GstSample* sample = gst_app_sink_try_pull_preroll(GST_APP_SINK(appSinkAudio), 100'000'000);
            if (sample)
            {
                GstBuffer* buffer = gst_sample_get_buffer(sample);
                gsize size = gst_buffer_get_size(buffer);
                batchSize = size / (sizeof(float) * audioChannels);
                MXL_INFO("Initial audio buffer size: {} samples", batchSize);
                gst_sample_unref(sample);
            }
            else
            {
                MXL_WARN("No preroll sample received while pulling from appSinkAudio. Unable to determine batchSize.");
            }

            mxlFlowConfigInfo configInfo;
            auto res = mxlCreateFlow(mxlInstance, flowDef.c_str(), getFlowOptions(batchSize, batchSize).c_str(), &configInfo);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow: {}", (int)res);
                return false;
            }

            res = mxlCreateFlowWriter(mxlInstance, uuids::to_string(audioFlowId).c_str(), nullptr, &flowWriterAudio);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow writer: {}", (int)res);
                return false;
            }

            MXL_INFO("Audio flow : {}", uuids::to_string(audioFlowId));
        }

        return true;
    }

    bool start()
    {
        //
        // Start the pipeline
        //
        auto baseTime = mxlIndexToTimestamp(&audioGrainRate, mxlGetCurrentIndex(&audioGrainRate) + 1);
        gst_element_set_base_time(pipeline, baseTime);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        gstBaseTime = gst_element_get_base_time(pipeline);
        MXL_INFO("media-pipeline: Gst base time: {} ns", gstBaseTime);
        running = true;

        //
        // Create the video and audio threads to pull samples from the appsink
        //
        videoThreadPtr = appSinkVideo ? std::make_unique<std::thread>(&LoopingFilePlayer::videoThread, this) : nullptr;
        audioThreadPtr = appSinkAudio ? std::make_unique<std::thread>(&LoopingFilePlayer::audioThread, this) : nullptr;

        return true;
    }

    void stop()
    {
        running = false;
    }

    bool isRunning() const
    {
        return running;
    }

private:
    static uuids::uuid createVideoFlowJson(std::string const& in_uri, int in_width, int in_height, mxlRational in_rate, bool in_progressive,
        std::string const& in_colorspace, std::string& out_flowDef)
    {
        auto root = picojson::object{};
        auto label = std::string{"Video flow for "} + in_uri;
        root["description"] = picojson::value(label);

        auto id = uuids::uuid_system_generator{}();
        root["id"] = picojson::value(uuids::to_string(id));
        root["tags"] = picojson::value(picojson::object());
        root["format"] = picojson::value("urn:x-nmos:format:video");
        root["label"] = picojson::value(label);
        root["parents"] = picojson::value(picojson::array());
        root["media_type"] = picojson::value("video/v210");

        auto tags = picojson::object{};
        auto groupHint = picojson::array{};
        groupHint.emplace_back(picojson::value{"Looping Source:Video"});
        tags["urn:x-nmos:tag:grouphint/v1.0"] = picojson::value(groupHint);
        root["tags"] = picojson::value(tags);

        auto grain_rate = picojson::object{};
        grain_rate["numerator"] = picojson::value(static_cast<double>(in_rate.numerator));
        grain_rate["denominator"] = picojson::value(static_cast<double>(in_rate.denominator));
        root["grain_rate"] = picojson::value(grain_rate);

        root["frame_width"] = picojson::value(static_cast<double>(in_width));
        root["frame_height"] = picojson::value(static_cast<double>(in_height));
        root["interlace_mode"] = picojson::value(in_progressive ? "progressive" : "interlaced_tff"); // todo. handle bff.
        root["colorspace"] = picojson::value(in_colorspace);

        auto components = picojson::array{};
        auto add_component = [&](std::string const& name, int w, int h)
        {
            auto comp = picojson::object{};
            comp["name"] = picojson::value(name);
            comp["width"] = picojson::value(static_cast<double>(w));
            comp["height"] = picojson::value(static_cast<double>(h));
            comp["bit_depth"] = picojson::value(10.0);
            components.emplace_back(comp);
        };

        add_component("Y", in_width, in_height);
        add_component("Cb", in_width / 2, in_height);
        add_component("Cr", in_width / 2, in_height);

        root["components"] = picojson::value(components);

        out_flowDef = picojson::value(root).serialize(true);
        return id;
    }

    static uuids::uuid createAudioFlowJson(std::string const& in_uri, mxlRational in_rate, int ch_count, int depth, std::string const& format,
        std::string& out_flowDef)
    {
        auto root = picojson::object{};
        auto label = std::string{"Audio flow for "} + in_uri;
        root["description"] = picojson::value(label);

        auto id = uuids::uuid_system_generator{}();
        root["id"] = picojson::value(uuids::to_string(id));
        root["tags"] = picojson::value(picojson::object());
        root["format"] = picojson::value("urn:x-nmos:format:audio");
        root["label"] = picojson::value(label);
        root["parents"] = picojson::value(picojson::array());
        root["media_type"] = picojson::value("audio/" + format);

        auto tags = picojson::object{};
        auto groupHint = picojson::array{};
        groupHint.emplace_back("Looping Source:Audio");
        tags["urn:x-nmos:tag:grouphint/v1.0"] = picojson::value(groupHint);
        root["tags"] = picojson::value(tags);

        auto sample_rate = picojson::object{};
        sample_rate["numerator"] = picojson::value(static_cast<double>(in_rate.numerator));
        root["sample_rate"] = picojson::value(sample_rate);

        root["channel_count"] = picojson::value(static_cast<double>(ch_count));
        root["bit_depth"] = picojson::value(static_cast<double>(depth));

        out_flowDef = picojson::value(root).serialize(true);
        return id;
    }

    static std::string getFlowOptions(std::uint32_t maxCommitBatchSizeHint, std::uint32_t maxSyncBatchSizeHint)
    {
        auto root = picojson::object{};
        root["maxCommitBatchSizeHint"] = picojson::value(static_cast<double>(maxCommitBatchSizeHint));
        root["maxSyncBatchSizeHint"] = picojson::value(static_cast<double>(maxSyncBatchSizeHint));
        return picojson::value(root).serialize(true);
    }

    void videoThread()
    {
        while (running)
        {
            auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appSinkVideo), 100'000'000);
            if (sample)
            {
                auto buffer = gst_sample_get_buffer(sample);
                if (buffer)
                {
                    auto pts = GST_BUFFER_PTS(buffer);
                    if (!_internalOffset)
                    {
                        _internalOffset = mxlGetTime() - (pts + gstBaseTime);
                        MXL_INFO("media-pipeline: Set internal offset to {} ns", *_internalOffset);
                    }

                    GST_BUFFER_PTS(buffer) = pts + gstBaseTime + *_internalOffset;
                    auto grainIndex = mxlTimestampToIndex(&videoGrainRate, GST_BUFFER_PTS(buffer));

                    lastVideoGrainIndex = grainIndex;

                    if (lastVideoGrainIndex == 0)
                    {
                        lastVideoGrainIndex = grainIndex;
                    }
                    else if (grainIndex != lastVideoGrainIndex + 1)
                    {
                        MXL_WARN("Video skipped grain index. Expected {}, got {}", lastVideoGrainIndex + 1, grainIndex);
                    }

                    if (GST_CLOCK_TIME_IS_VALID(pts))
                    {
                        [[maybe_unused]]
                        int64_t frame = currentFrame++;
                        MXL_TRACE("Video frame received.  Frame {}, pts (ms) {}, duration (ms) {}",
                            frame,
                            pts / GST_MSECOND,
                            GST_BUFFER_DURATION(buffer) / GST_MSECOND);
                    }

                    GstMapInfo map;
                    if (gst_buffer_map(buffer, &map, GST_MAP_READ))
                    {
                        /// Open the grain.
                        mxlGrainInfo gInfo;
                        uint8_t* mxl_buffer = nullptr;

                        /// Open the grain for writing.
                        if (mxlFlowWriterOpenGrain(flowWriterVideo, grainIndex, &gInfo, &mxl_buffer) != MXL_STATUS_OK)
                        {
                            MXL_ERROR("Failed to open grain at index '{}'", grainIndex);
                            break;
                        }

                        gInfo.validSlices = gInfo.totalSlices;
                        ::memcpy(mxl_buffer, map.data, map.size);

                        if (mxlFlowWriterCommitGrain(flowWriterVideo, &gInfo) != MXL_STATUS_OK)
                        {
                            MXL_ERROR("Failed to open grain at index '{}'", grainIndex);
                            break;
                        }

                        gst_buffer_unmap(buffer, &map);
                    }

                    auto ns = mxlGetNsUntilIndex(grainIndex, &videoGrainRate);
                    mxlSleepForNs(ns);
                }
                gst_sample_unref(sample);
            }
            else
            {
                MXL_WARN("No sample received while pulling from appsink");
            }
        }
    }

    void audioThread()
    {
        while (running)
        {
            auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appSinkAudio), 100'000'000);
            if (sample)
            {
                auto buffer = gst_sample_get_buffer(sample);
                if (buffer)
                {
                    auto pts = GST_BUFFER_PTS(buffer);
                    if (!_internalOffset)
                    {
                        _internalOffset = mxlGetTime() - (pts + gstBaseTime);
                        MXL_INFO("media-pipeline: Set internal offset to {} ns", *_internalOffset);
                    }

                    GST_BUFFER_PTS(buffer) = pts + gstBaseTime + *_internalOffset;
                    auto grainIndex = mxlTimestampToIndex(&audioGrainRate, GST_BUFFER_PTS(buffer));

                    lastAudioGrainIndex = grainIndex;

                    if (lastAudioGrainIndex == 0)
                    {
                        lastAudioGrainIndex = grainIndex;
                    }
                    else if (grainIndex != lastAudioGrainIndex + 1)
                    {
                        MXL_WARN("Audio skipped grain index. Expected {}, got {}", lastAudioGrainIndex + 1, grainIndex);
                    }

                    GstMapInfo map_info;
                    if (gst_buffer_map(buffer, &map_info, GST_MAP_READ))
                    {
                        auto nbSamplesPerChan = map_info.size / (sizeof(float) * audioChannels);

                        mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
                        if (mxlFlowWriterOpenSamples(flowWriterAudio, grainIndex, nbSamplesPerChan, &payloadBuffersSlices))
                        {
                            MXL_ERROR("Failed to open samples at index '{}'", grainIndex);
                            break;
                        }

                        std::uintptr_t offset = 0;
                        for (uint64_t chan = 0; chan < payloadBuffersSlices.count; ++chan)
                        {
                            for (auto& fragment : payloadBuffersSlices.base.fragments)
                            {
                                if (fragment.size > 0)
                                {
                                    auto dst = reinterpret_cast<std::uint8_t*>(fragment.pointer) + (chan * payloadBuffersSlices.stride);
                                    auto src = map_info.data + offset;
                                    ::memcpy(dst, src, fragment.size);
                                    offset += fragment.size;
                                }
                            }
                        }

                        if (mxlFlowWriterCommitSamples(flowWriterAudio) != MXL_STATUS_OK)
                        {
                            MXL_ERROR("Failed to open samples at index '{}'", grainIndex);
                            break;
                        }

                        gst_buffer_unmap(buffer, &map_info);
                    }

                    auto ns = mxlGetNsUntilIndex(grainIndex, &audioGrainRate);
                    mxlSleepForNs(ns);
                }
                gst_sample_unref(sample);
            }
            else
            {
                MXL_WARN("No sample received while pulling from appsink");
            }
        }
    }

    // The URI GST PlayBin will use to play the video
    std::string uri;
    // The MXL video flow id
    uuids::uuid videoFlowId;
    // The MXL audio flow id
    uuids::uuid audioFlowId;
    // Unique pointer to video processing thread
    std::unique_ptr<std::thread> videoThreadPtr;
    // Unique pointer to audio processing thread
    std::unique_ptr<std::thread> audioThreadPtr;
    // The MXL domain
    std::string domain;
    // GStreamer base time
    std::uint64_t gstBaseTime{0};
    // Video flow writer allocated by the MXL instance
    ::mxlFlowWriter flowWriterVideo = nullptr;
    // Audio flow writer allocated by the MXL instance
    ::mxlFlowWriter flowWriterAudio = nullptr;
    // The MXL instance
    ::mxlInstance mxlInstance = nullptr;
    // Offset between Gstreamer and MXL
    std::optional<std::uint64_t> _internalOffset{std::nullopt};
    // GStreamer media pipeline
    ::GstElement* pipeline = nullptr;
    // GStreamer appsink for Video
    ::GstElement* appSinkVideo = nullptr;
    // GStreamer appsink for Audio
    ::GstElement* appSinkAudio = nullptr;
    // Keep a copy of the last video grain index
    uint64_t lastVideoGrainIndex = 0;
    // Keep a copy of the last video grain index
    uint64_t lastAudioGrainIndex = 0;
    // Running flag
    std::atomic<bool> running{false};
    // Current frame number
    std::atomic<int64_t> currentFrame{0};
    // The video grain rate
    ::mxlRational videoGrainRate{0, 1};
    // The audio grain rate
    ::mxlRational audioGrainRate{defaultAudioGrainRate};
    // Audio channels
    std::uint32_t audioChannels = 0;
};

int main(int argc, char* argv[])
{
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    //
    // Command line argument parsing
    //
    std::string inputFile, domain;

    CLI::App cli{"mxl-gst-looping-filesrc"};
    auto domainOpt = cli.add_option("-d,--domain", domain, "The MXL domain directory")->required();
    domainOpt->required(true);

    auto inputOpt = cli.add_option("-i,--input", inputFile, "MPEGTS media file location")->required();
    inputOpt->required(true);
    inputOpt->check(CLI::ExistingFile);

    CLI11_PARSE(cli, argc, argv);

    //
    // Initialize GStreamer
    //
    gst_init(&argc, &argv);

    // Simple scope guard to ensure GStreamer is de-initialized.
    // Replace with std::scope_exit when widely available ( C++23 )
    auto onExit = std::unique_ptr<void, void (*)(void*)>(nullptr, [](void*) { gst_deinit(); });

    //
    // Create the Player and open the input uri
    //
    auto player = std::make_unique<LoopingFilePlayer>(domain);
    if (!player->open(inputFile))
    {
        MXL_ERROR("Failed to open input file: {}", inputFile);
        return -1;
    }

    //
    // Start the player
    //
    if (!player->start())
    {
        MXL_ERROR("Failed to start the player");
        return -1;
    }

    while (!g_exit_requested && player->isRunning())
    {
        // Wait for the player to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (player->isRunning())
    {
        player->stop();
    }

    // Release the player
    player.reset();

    return 0;
}
