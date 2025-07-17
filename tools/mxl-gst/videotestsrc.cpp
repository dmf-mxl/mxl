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
    Rational frame_rate{30, 1};
    uint64_t pattern{0};
    std::string textoverlay{"EBU DMF MXL"};
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

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

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

    GstreamerPipelineConfig gst_config{
        .frame_width = static_cast<uint64_t>(descriptor_parser.get<double>("frame_width")),
        .frame_height = static_cast<uint64_t>(descriptor_parser.get<double>("frame_height")),
        .frame_rate = frame_rate,
        .pattern = pattern_map.at(pattern),
        .textoverlay = textOverlay,
    };

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
    FlowInfo fInfo;
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
                    GrainInfo gInfo;
                    uint8_t* mxl_buffer = nullptr;
                    /// Open the grain for writing.
                    if (mxlFlowWriterOpenGrain(writer, grain_index, &gInfo, &mxl_buffer) != MXL_STATUS_OK)
                    {
                        MXL_ERROR("Failed to open grain at index '{}'", grain_index);
                        break;
                    }

                    ::memcpy(mxl_buffer, map_info.data, gInfo.grainSize);

                    gInfo.commitedSize = gInfo.grainSize;
                    if (mxlFlowWriterCommitGrain(writer, &gInfo) != MXL_STATUS_OK)
                    {
                        MXL_ERROR("Failed to open grain at index '{}'", grain_index);
                        break;
                    }

                    gst_buffer_unmap(gst_buffer, &map_info);
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