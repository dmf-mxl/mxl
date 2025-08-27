// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "../../lib/src/internal/FlowParser.hpp"
#include "../../lib/src/internal/Logging.hpp"

std::sig_atomic_t volatile g_exit_requested = 0;

void signal_handler(int)
{
    g_exit_requested = 1;
}

struct GstreamerPipelineConfig
{
    uint64_t frame_width{1920};
    uint64_t frame_height{1080};
    mxlRational frame_rate{30, 1};
    uint64_t pattern{0};
    std::string textoverlay{"EBU DMF MXL"};
    uint64_t bit_depth{10};
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

class GstreamerPipeline final
{
public:
    GstreamerPipeline(GstreamerPipelineConfig const& config)
    {
        gst_init(nullptr, nullptr);

        _videotestsrc = gst_element_factory_make("videotestsrc", "videotestsrc");
        if (!_videotestsrc)
        {
            throw std::runtime_error("Gstreamer: 'videotestsrc' could not be created.");
        }

        g_object_set(_videotestsrc, "is-live", TRUE, "pattern", config.pattern, nullptr);

        _clockoverlay = gst_element_factory_make("clockoverlay", "clockoverlay");
        if (!_clockoverlay)
        {
            throw std::runtime_error("Gstreamer: 'clockoverlay' could not be created.");
        }

        _textoverlay = gst_element_factory_make("textoverlay", "textoverlay");
        if (!_textoverlay)
        {
            throw std::runtime_error("Gstreamer: 'textoverlay' could not be created.");
        }
        g_object_set(_textoverlay, "text", config.textoverlay.c_str(), "font-desc", "Sans, 36", nullptr);

        _videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
        if (!_videoconvert)
        {
            throw std::runtime_error("Gstreamer: 'videoconvert' could not be created.");
        }

        _videoscale = gst_element_factory_make("videoscale", "videoscale");
        if (!_videoscale)
        {
            throw std::runtime_error("Gstreamer: 'videoscale' could not be created.");
        }

        _appsink = gst_element_factory_make("appsink", "appsink");
        if (!_appsink)
        {
            throw std::runtime_error("Gstreamer: 'appsink' could not be created.");
        }

        // what format to use, 10 bit v210, 16bit v216
        std::string pixel_format = "v210";
        if( config.bit_depth == 16 )
            pixel_format = "v216";

        printf("Pixel format %s\n", pixel_format.c_str() );

        // Configure appsink
        g_object_set(G_OBJECT(_appsink),
            "caps",
            gst_caps_new_simple("video/x-raw",
                "format",
                G_TYPE_STRING,
                pixel_format,
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
        _pipeline = gst_pipeline_new("sink-pipeline");
        if (!_pipeline)
        {
            throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
        }

        // Build the pipeline
        gst_bin_add_many(GST_BIN(_pipeline), _videotestsrc, _videoconvert, _videoscale, _clockoverlay, _textoverlay, _appsink, nullptr);
        if (!_pipeline)
        {
            throw std::runtime_error("Gstreamer: could not add elements to the pipeline");
        }
        if (gst_element_link_many(_videotestsrc, _videoconvert, _videoscale, _clockoverlay, _textoverlay, _appsink, nullptr) != TRUE)
        {
            throw std::runtime_error("Gstreamer: elements could not be linked.");
        }
    }

    ~GstreamerPipeline()
    {
        if (_pipeline)
        {
            gst_element_set_state(_pipeline, GST_STATE_NULL);
            gst_object_unref(_pipeline);
        }
        if (_videotestsrc)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_videotestsrc) > 0)
            {
                gst_object_unref(_videotestsrc);
            }
        }

        if (_videoconvert)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_videoconvert) > 0)
            {
                gst_object_unref(_videoconvert);
            }
        }

        if (_videoscale)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_videoscale) > 0)
            {
                gst_object_unref(_videoscale);
            }
        }

        if (_clockoverlay)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_clockoverlay) > 0)
            {
                gst_object_unref(_clockoverlay);
            }
        }

        if (_textoverlay)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_textoverlay) > 0)
            {
                gst_object_unref(_textoverlay);
            }
        }

        if (_appsink)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_appsink))
            {
                gst_object_unref(_appsink);
            }
        }
    }

    void start()
    {
        gst_element_set_state(_pipeline, GST_STATE_PLAYING);
    }

    GstSample* pullSample()
    {
        return gst_app_sink_pull_sample(GST_APP_SINK(_appsink));
    }

private:
    GstElement* _videotestsrc{nullptr};
    GstElement* _videoconvert{nullptr};
    GstElement* _videoscale{nullptr};
    GstElement* _clockoverlay{nullptr};
    GstElement* _textoverlay{nullptr};
    GstElement* _appsink{nullptr};
    GstElement* _pipeline{nullptr};
};

void log_grain(mxlGrainInfo &gInfo)
{
    printf("videotestsrc.cpp: size %u flags %x location %d device index %d grain size %u committed %u\n",
        gInfo.size, gInfo.flags, gInfo.payloadLocation, gInfo.deviceIndex, gInfo.grainSize, gInfo.commitedSize);
    printf("payload location %u device index %d grain size %u grain index %lu grain time stamp %lu\n",
        gInfo.payloadLocation, gInfo.deviceIndex, gInfo.grainSize, gInfo.grainIndex, gInfo.grainTimeStamp);
}

void copy_packed_to_planar_16_yuv_422(uint16_t *dest_planar, const uint16_t *src_packed, uint64_t frame_width, uint64_t frame_height)
{
    // get pointers to planes and copy
    uint16_t *y = dest_planar;
    uint16_t *u = y + frame_width * frame_height / 2;
    uint16_t *v = u + frame_width * frame_height / 2;

    // simple iteration
    uint64_t loops = frame_width * frame_height / 2;

    while(loops-- > 0)
    {
        *u++ = *src_packed++;
        *y++ = *src_packed++;
        *v++ = *src_packed++;
        *y++ = *src_packed++;
    }
}

typedef struct ColourBars
{
    uint8_t colours[8][4];
} ColourBars;

static const ColourBars bars_75_percent_8_bit = {
    180, 128, 128, 255, /* 75% white */
    162, 44, 142, 255, /* 75% yellow */
    131, 156, 44, 255, /* 75% cyan */
    112, 72, 58, 255, /* 75% green */
    84, 184, 198, 255, /* 75% magenta */
    65, 100, 212, 255, /* 75% red */
    35, 212, 114, 255, /* 75% blue */
    16, 128, 128, 255 /* black */
};

static uint16_t * fill_pixels( uint16_t *buffer, uint32_t width, uint16_t value )
{
    // just fill with value width times
    for( uint32_t i = 0; i < width; i++ )
    {
        *buffer++ = value;
    }

    return buffer;
}

static void fill_planar_bars( uint16_t *buffer, uint32_t width, uint32_t height )
{
    uint32_t y_bar_width = width / 8;
    uint32_t uv_bar_width = (width / 8) / 2;

    printf("fill_planar_bars(): buffer %p width %u height %u\n", (void*)buffer, width, height );

    if( buffer )
    {
        printf("fill_planar_bars(): Y\n");

        // assume a contiguous planar buufer, fill y then u then v
        for( uint32_t i = 0; i < height; i++ )
        {
            // fill each colour value in turn
            for( uint32_t j = 0; j < 8; j++ )
            {
                buffer = fill_pixels( buffer, y_bar_width, bars_75_percent_8_bit.colours[j][0] * 256 );
            }
        }

        printf("fill_planar_bars(): U\n");

        for( uint32_t i = 0; i < height; i++ )
        {
            // fill each colour value in turn
            for( uint32_t j = 0; j < 8; j++ )
            {
                buffer = fill_pixels( buffer, uv_bar_width, bars_75_percent_8_bit.colours[j][1] * 256 );
            }
        }

        printf("fill_planar_bars(): V\n");

        for( uint32_t i = 0; i < height; i++ )
        {
            // fill each colour value in turn
            for( uint32_t j = 0; j < 8; j++ )
            {
                buffer = fill_pixels( buffer, uv_bar_width, bars_75_percent_8_bit.colours[j][2] * 256 );
            }
        }

        printf("fill_planar_bars(): complete\n");

    }
    else
    {
        printf("fill_planar_bars(): no buffer provided\n");
    }
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);
    struct timespec grain_time;
    uint64_t grain_time_ns = 0;
    uint64_t initial_index = 0;

    // start at relative frame zero
    uint64_t frame_count = 0;


    CLI::App app("mxl-gst-videotestsrc");

    std::string flowConfigFile;
    auto flowConfigFileOpt = app.add_option("-f, --flow-config-file", flowConfigFile, "The json file which contains the NMOS Flow configuration");
    flowConfigFileOpt->required(true);

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

    std::ifstream file(flowConfigFile, std::ios::in | std::ios::binary);
    if (!file)
    {
        MXL_ERROR("Failed to open file: '{}'", flowConfigFile);
        return EXIT_FAILURE;
    }
    std::string flow_descriptor{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    mxl::lib::FlowParser descriptor_parser{flow_descriptor};

    auto frame_rate = descriptor_parser.getGrainRate();
    auto flowID = uuids::to_string(descriptor_parser.getId());
    auto bit_depth = descriptor_parser.getVideoComponentBitDepth();

    GstreamerPipelineConfig gst_config{
        .frame_width = static_cast<uint64_t>(descriptor_parser.get<double>("frame_width")),
        .frame_height = static_cast<uint64_t>(descriptor_parser.get<double>("frame_height")),
        .frame_rate = frame_rate,
        .pattern = pattern_map.at(pattern),
        .textoverlay = textOverlay,
        .bit_depth = bit_depth,
    };

    printf("bit depth %lu w %lu h %lu\n", bit_depth, gst_config.frame_width, gst_config.frame_height );

    GstreamerPipeline gst_pipeline(gst_config);

    mxlStatus ret;
    int exit_status = EXIT_SUCCESS;

    auto instance = mxlCreateInstance(domain.c_str(), "");
    if (instance == nullptr)
    {
        MXL_ERROR("Failed to create MXL instance");
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    // Create the flow
    mxlFlowInfo fInfo;
    ret = mxlCreateFlow(instance, flow_descriptor.c_str(), nullptr, &fInfo);
    if (ret != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create flow with status '{}'", static_cast<int>(ret));
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    // Create the flow writer
    mxlFlowWriter writer;
    if (mxlCreateFlowWriter(instance, flowID.c_str(), "", &writer) != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create flow write");
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    gst_pipeline.start();

    GstSample* gst_sample;
    GstBuffer* gst_buffer;

    setlocale(LC_NUMERIC, "");
    printf("test loop starts\n");

    // get time now (tai willbe close but out by 35 apporx seconds unless corrected on host)
    clock_gettime(CLOCK_TAI, &grain_time);

    // compute time in nano seconds
    grain_time_ns = grain_time.tv_sec * 1000000000;
    grain_time_ns += grain_time.tv_nsec;

    initial_index = mxlTimestampToIndex(&frame_rate, grain_time_ns);

    while (!g_exit_requested)
    {
        gst_sample = gst_pipeline.pullSample();
        if (gst_sample)
        {
            uint64_t grain_index = frame_count + initial_index;

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
                    if (mxlFlowWriterOpenGrain(writer, grain_index, &gInfo, &mxl_buffer) != MXL_STATUS_OK)
                    {
                        MXL_ERROR("Failed to open grain at index '{}'", grain_index);
                        break;
                    }

                    // set grain index and time stamp
                    gInfo.grainIndex = grain_index;
                    gInfo.grainTimeStamp = mxlIndexToTimestamp(&frame_rate, gInfo.grainIndex);

                    log_grain(gInfo);

                    // convert to planar if 16 bit
                    if( bit_depth == 16 )
                    {
                        // copy_packed_to_planar_16_yuv_422((uint16_t*)mxl_buffer, (uint16_t*)map_info.data, gst_config.frame_width, gst_config.frame_height);

                        fill_planar_bars( (uint16_t*)mxl_buffer, gst_config.frame_width, gst_config.frame_height );

                    }
                    else
                    {
                        ::memcpy(mxl_buffer, map_info.data, gInfo.grainSize);
                    }

                    gInfo.commitedSize = gInfo.grainSize;
                    if (mxlFlowWriterCommitGrain(writer, &gInfo) != MXL_STATUS_OK)
                    {
                        MXL_ERROR("Failed to open grain at index '{}'", grain_index);
                        break;
                    }

                    gst_buffer_unmap(gst_buffer, &map_info);

                    // next frame
                    frame_count++;
                }
                gst_buffer_unref(gst_buffer);
            }

            gst_sample_unref(gst_sample);

            auto ns = mxlGetNsUntilIndex(grain_index++, &frame_rate);
            mxlSleepForNs(ns);
        }
    }

mxl_cleanup:
    if (instance != nullptr)
    {
        // clean-up mxl objects
        mxlReleaseFlowWriter(instance, writer);
        mxlDestroyFlow(instance, flowID.c_str());
        mxlDestroyInstance(instance);
    }

    return exit_status;
}