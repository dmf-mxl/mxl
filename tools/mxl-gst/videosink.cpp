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

struct GstreamerPipelineConfig
{
    uint64_t frame_width;
    uint64_t frame_height;
    Rational frame_rate;
};

class GstreamerPipeline final
{
public:
    GstreamerPipeline(GstreamerPipelineConfig const& config)
    {
        gst_init(nullptr, nullptr);

        // Create the elements
        _appsrc = gst_element_factory_make("appsrc", "source");
        if (!_appsrc)
        {
            throw std::runtime_error("Gstreamer: 'appsrc' could not be created.");
        }
        // Configure appsrc
        g_object_set(G_OBJECT(_appsrc),
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
            "format",
            GST_FORMAT_TIME,
            nullptr);

        _videoconvert = gst_element_factory_make("videoconvert", "convert");
        if (!_videoconvert)
        {
            throw std::runtime_error("Gstreamer: 'videoconvert' could not be created.");
        }

        _videoscale = gst_element_factory_make("videoscale", "scale");
        if (!_videoscale)
        {
            throw std::runtime_error("Gstreamer: 'videoscale' could not be created.");
        }

        _autovideosink = gst_element_factory_make("autovideosink", "sink");
        if (!_autovideosink)
        {
            throw std::runtime_error("Gstreamer: 'autovideosink' could not be created.");
        }

        // Create the empty pipeline
        _pipeline = gst_pipeline_new("test-pipeline");
        if (!_pipeline)
        {
            throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
        }

        // Build the pipeline
        gst_bin_add_many(GST_BIN(_pipeline), _appsrc, _videoconvert, _videoscale, _autovideosink, nullptr);
        if (gst_element_link_many(_appsrc, _videoconvert, _videoscale, _autovideosink, nullptr) != TRUE)
        {
            throw std::runtime_error("Gstreamer: Elements could not be linked.");
        }
    }

    ~GstreamerPipeline()
    {
        if (_pipeline)
        {
            gst_element_set_state(_pipeline, GST_STATE_NULL);
            gst_object_unref(_pipeline);
        }
        if (_appsrc)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_appsrc) > 0)
            {
                gst_object_unref(_appsrc);
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

        if (_autovideosink)
        {
            if (GST_OBJECT_REFCOUNT_VALUE(_autovideosink))
            {
                gst_object_unref(_autovideosink);
            }
        }
    }

    void start()
    {
        // Start playing
        gst_element_set_state(_pipeline, GST_STATE_PLAYING);
    }

    void pushSample(GstBuffer* gst_buffer, uint8_t* payload, size_t payload_len)
    {
        GstMapInfo map;
        gst_buffer_map(gst_buffer, &map, GST_MAP_WRITE);

        ::memcpy(map.data, payload, payload_len);

        int ret;
        g_signal_emit_by_name(_appsrc, "push-buffer", gst_buffer, &ret);
        if (ret != GST_FLOW_OK)
        {
            MXL_ERROR("Error pushing buffer to appsrc");
            return;
        }
        gst_buffer_unmap(gst_buffer, &map);
    }

private:
    GstElement* _appsrc{nullptr};
    GstElement* _videoconvert{nullptr};
    GstElement* _videoscale{nullptr};
    GstElement* _autovideosink{nullptr};
    GstElement* _pipeline{nullptr};
};

int real_main(int argc, char** argv, void*)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-gst-videosink");

    std::string flowID;
    auto flowIDOpt = app.add_option("-f, --flow-id", flowID, "The flow ID");
    flowIDOpt->required(true);

    std::string domain;
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    CLI11_PARSE(app, argc, argv);

    // So the source need to generate a json?
    auto const descriptor_path = mxl::lib::makeFlowDescriptorFilePath(domain, flowID);
    if (!std::filesystem::exists(descriptor_path))
    {
        MXL_ERROR("Flow descriptor file '{}' does not exist", descriptor_path.string());
        return EXIT_FAILURE;
    }

    std::ifstream descriptor_reader(descriptor_path);
    std::string flow_descriptor((std::istreambuf_iterator<char>(descriptor_reader)), std::istreambuf_iterator<char>());
    mxl::lib::FlowParser descriptor_parser{flow_descriptor};

    GstreamerPipelineConfig gst_config{.frame_width = static_cast<uint64_t>(descriptor_parser.get<double>("frame_width")),
        .frame_height = static_cast<uint64_t>(descriptor_parser.get<double>("frame_height")),
        .frame_rate = descriptor_parser.getGrainRate()};

    GstreamerPipeline gst_pipeline(gst_config);
    GstBuffer* gst_buffer{nullptr};

    int exit_status = EXIT_SUCCESS;
    mxlStatus ret;

    Rational rate;
    std::uint64_t editUnitDurationNs = 33'000'000LL;
    std::uint64_t grain_index = 0;

    auto instance = mxlCreateInstance(domain.c_str(), "");
    if (instance == nullptr)
    {
        MXL_ERROR("Failed to create MXL instance");
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    // Create a flow reader for the given flow id.
    mxlFlowReader reader;
    ret = mxlCreateFlowReader(instance, flowID.c_str(), "", &reader);
    if (ret != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create flow reader with status '{}'", static_cast<int>(ret));
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    // Extract the FlowInfo structure.
    FlowInfo flow_info;
    ret = mxlFlowReaderGetInfo(reader, &flow_info);
    if (ret != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to get flow info with status '{}'", static_cast<int>(ret));
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    rate = flow_info.discrete.grainRate;
    editUnitDurationNs = static_cast<std::uint64_t>(1.0 * rate.denominator * (1'000'000'000.0 / rate.numerator));
    editUnitDurationNs += 1'000'000ULL; // allow some margin.
    gst_pipeline.start();

    grain_index = mxlGetCurrentIndex(&flow_info.discrete.grainRate);
    while (!g_exit_requested)
    {
        GrainInfo grain_info;
        uint8_t* payload;
        auto ret = mxlFlowReaderGetGrain(reader, grain_index, editUnitDurationNs, &grain_info, &payload);
        if (ret != MXL_STATUS_OK)
        {
            // Missed a grain. resync.
            MXL_ERROR("Missed grain {}, err : {}", grain_index, (int)ret);
            grain_index = mxlGetCurrentIndex(&flow_info.discrete.grainRate);
            continue;
        }
        else if (grain_info.commitedSize != grain_info.grainSize)
        {
            // we don't need partial grains. wait for the grain to be complete.
            continue;
        }

        grain_index++;

        if (!gst_buffer)
        {
            gst_buffer = gst_buffer_new_allocate(nullptr, grain_info.grainSize, nullptr);
        }

        // check if the buffer is writable to avoid segmentation fault
        // https://github.com/moontree/gstreamer-usage
        if (!gst_buffer_is_writable(gst_buffer))
        {
            gst_buffer_unref(gst_buffer);
            gst_buffer = gst_buffer_new_allocate(nullptr, grain_info.grainSize, nullptr);
        }
        gst_pipeline.pushSample(gst_buffer, payload, grain_info.grainSize);
    }

mxl_cleanup:
    if (instance != nullptr)
    {
        // clean-up mxl objects
        mxlReleaseFlowReader(instance, reader);
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
