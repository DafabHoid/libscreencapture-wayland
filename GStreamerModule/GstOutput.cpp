#include "GstOutput.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <string>

using namespace std::chrono_literals;

namespace gstreamer
{

static GstVideoFormat pixelFormat2Gst(PixelFormat format)
{
	switch (format)
	{
	case PixelFormat::BGRA:
		return GST_VIDEO_FORMAT_BGRA;
	case PixelFormat::RGBA:
		return GST_VIDEO_FORMAT_RGBA;
	case PixelFormat::BGRX:
		return GST_VIDEO_FORMAT_BGRx;
	case PixelFormat::RGBX:
		return GST_VIDEO_FORMAT_RGBx;
	}
}

static void errorGst(GstBus* bus, GstMessage* msg)
{
	GError* err;
	char* debugInfo;
	gst_message_parse_error(msg, &err, &debugInfo);
	g_printerr("Error received from %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
	g_printerr("Dbg Info %s\n", debugInfo);
	g_clear_error(&err);
	g_free(debugInfo);
}

static void pollMessages(GstElement* pipeline) noexcept
{
	GstBus* bus = gst_element_get_bus(pipeline);
	GstMessage* msg;
	while ((msg = gst_bus_pop(bus)) != nullptr)
	{
		switch (GST_MESSAGE_TYPE(msg))
		{
		case GST_MESSAGE_ERROR:
			errorGst(bus, msg);
			break;
		case GST_MESSAGE_STREAM_STATUS:
			break;
		case GST_MESSAGE_STATE_CHANGED:
			if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline))
			{
				GstState old_state, new_state, pending_state;
				gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
				g_print ("\nPipeline state changed from %s to %s:\n",
				         gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
			}
			break;
		default:
			fprintf(stdout, "Received message, type %s\n", GST_MESSAGE_TYPE_NAME(msg));
		}
		gst_message_unref(msg);
	}
	gst_object_unref(bus);
}

GstOutput::GstOutput(Rect sourceSize, PixelFormat sourceFormat, Rect scaledSize,
                     const std::string& hwDevicePath, Codec codec,
                     const std::string& outputPath, const std::string& outputFormat)
{
	const char* codecName;
	const char* codecParser;
	switch (codec)
	{
		case Codec::H264:
			codecName = "h264";
			codecParser = "h264parse";
			break;
		case Codec::H265:
			codecName = "h265";
			codecParser = "h265parse";
			break;
	}
	char pipelineDescription[600];
	snprintf(pipelineDescription, sizeof(pipelineDescription),
			"appsrc max-buffers=8 block=true name=appsrc ! video/x-raw, format=%s, width=%u, height=%u, framerate=0/1, interlace-mode=progressive "
			"! vaapipostproc width=%u height=%u ! vaapi%senc quality-level=6 rate-control=cqp init-qp=26 name=encoder "
			"! %s ! queue max-size-buffers=8 ! mpegtsmux name=mux ! filesink location=%s",
			gst_video_format_to_string(pixelFormat2Gst(sourceFormat)),
			sourceSize.w, sourceSize.h,
			scaledSize.w, scaledSize.h,
			codecName, codecParser,
			outputPath.c_str());

	GError* err {};
	this->pipeline = gst_parse_launch(pipelineDescription, &err);
	if (err)
	{
		std::string errorMessage = err->message;
		g_clear_error(&err);
		throw GStreamerException("Pipeline creation failed: %s", errorMessage.c_str());
	}
	this->appSource = gst_bin_get_by_name(GST_BIN(this->pipeline), "appsrc");
	// start the pipeline
	GstStateChangeReturn state = gst_element_set_state(this->pipeline, GST_STATE_PLAYING);
	if (state == GST_STATE_CHANGE_FAILURE)
	{
		throw GStreamerException("Starting gstreamer pipeline failed!");
	}
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(this->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline base");
}

GstOutput::~GstOutput() noexcept
{
	if (this->pipeline)
	{
		gst_element_set_state(this->pipeline, GST_STATE_PAUSED);
		pollMessages(this->pipeline);
		gst_element_set_state(this->pipeline, GST_STATE_NULL);
		gst_object_unref(this->pipeline);
	}
}

GstOutput::GstOutput(GstOutput&& o)
: pipeline(o.pipeline),
  appSource(o.appSource)
{
	o.pipeline = nullptr;
	o.appSource = nullptr;
}

GstOutput::Builder::Builder(common::Rect sourceSize, common::PixelFormat sourceFormat) noexcept
: sourceSize(sourceSize),
  sourceFormat(sourceFormat),
  targetSize(sourceSize),
  codec(Codec::H264)
{
}

GstOutput GstOutput::Builder::build()
{
	if (sourceSize.w == 0 || sourceSize.h == 0)
	{
		throw GStreamerException("Source frame dimensions must not be zero, got %ux%u", sourceSize.w, sourceSize.h);
	}
	if (targetSize.w == 0 || targetSize.h == 0)
	{
		throw GStreamerException("Scaled frame dimensions must not be zero, got %ux%u", targetSize.w, targetSize.h);
	}
	if (outputFormat.empty() && outputPath.empty())
	{
		throw GStreamerException("Neither output format nor output path specified");
	}
	if (hwDevicePath.empty())
	{
		throw GStreamerException("No hardware device path specified");
	}
	return GstOutput(sourceSize, sourceFormat, targetSize, hwDevicePath, codec, outputPath, outputFormat);
}

static void onFrameMemoryDropped(void* p)
{
	auto cb = static_cast<common::MemoryFrame*>(p);
	delete cb;
}

void GstOutput::pushFrame(std::unique_ptr<common::MemoryFrame> frame)
{
	GstBuffer* frameMem = gst_buffer_new_wrapped_full(
			static_cast<GstMemoryFlags>(GST_MEMORY_FLAG_READONLY | GST_MEMORY_FLAG_ZERO_PADDED),
			frame->memory,
			frame->stride * frame->height,
			0,
			frame->size,
			frame.get(),
			onFrameMemoryDropped);
	// release object from unique_ptr, it is now owned by the GstBuffer and released via onFrameMemoryDropped
	frame.release();
	pushFrame(frameMem);
}

void GstOutput::pushFrame(GstBuffer* buf)
{
	auto now = std::chrono::steady_clock::now();
			GST_BUFFER_TIMESTAMP(buf) = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	GST_BUFFER_DURATION(buf) = std::chrono::duration_cast<std::chrono::nanoseconds>(32ms).count();
	GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(this->appSource), buf);
	if (ret != GST_FLOW_OK)
	{
		throw GStreamerException("push failed: %d", ret);
	}

	pollMessages(this->pipeline);
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(this->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline last");
}

GStreamerException::GStreamerException(const char* messageFmtStr, ...) noexcept
{
	std::va_list v_args;
	va_start(v_args, messageFmtStr);
	std::vsnprintf(message, sizeof(message), messageFmtStr, v_args);
	va_end(v_args);

	dumpStackTrace();
}

}

struct Output
{
	GMainLoop* mainLoop;
	GstElement* screenSrc;
	GstElement* scaler;
	GstElement* encoder;
	GstElement* h264parse;
	GstElement* muxer;
	GstPad* muxerInput;
	GstElement* sink;
	GstElement* pipeline;
} gst;

#if 0

void initGstOutput(int width, int height, PixelFormat sourcePixelFormat)
{
	gst.screenSrc = gst_element_factory_make("appsrc", "screen source");
//	gst.scaler = gst_element_factory_make("vaapipostproc", "scaler");
	gst.scaler = gst_element_factory_make("videoscale", "scaler");
//	gst.encoder = gst_element_factory_make("vaapih264enc", "encoder");
	gst.encoder = gst_element_factory_make("x264enc", "encoder");
	gst.h264parse = gst_element_factory_make("h264parse", "h264parser");
	gst.muxer = gst_element_factory_make("matroskamux", "muxer");
	gst.sink = gst_element_factory_make("filesink", "sink");
	gst.pipeline = gst_pipeline_new("gpu-encoding");
	if (!gst.screenSrc || !gst.scaler || !gst.encoder || !gst.muxer || !gst.sink || !gst.pipeline)
	{
		throw GStreamerException("Could not create all of the required pipeline elements!");
	}

//	g_object_set(G_OBJECT(gst.scaler), "width", 1920, "height", 1080, nullptr);
//	g_object_set(G_OBJECT(gst.scaler), "format", GST_VIDEO_FORMAT_NV12, nullptr);

	g_object_set(G_OBJECT(gst.sink), "location", "out.mkv", nullptr);
	g_object_set(G_OBJECT(gst.muxer), "streamable", true, nullptr);

	gst_app_src_set_stream_type(GST_APP_SRC(gst.screenSrc), GST_APP_STREAM_TYPE_STREAM);
	g_object_set(G_OBJECT(gst.screenSrc), "is-live", true, nullptr);
	gst_app_src_set_latency(GST_APP_SRC(gst.screenSrc), 0, 1000);
	g_object_set(G_OBJECT(gst.screenSrc), "format", GST_FORMAT_TIME, nullptr);

	// create a new pad for the application source that matches the format of the PipeWire buffers
	// the buffers are later inserted into the pipeline through this virtual source
	GstVideoInfo sourceInfo;
	gst_video_info_set_format(&sourceInfo, pixelFormat2Gst(sourcePixelFormat), width, height);
	fprintf(stdout, "size: %zx, stride: %x %x\n", sourceInfo.size, sourceInfo.stride[0], sourceInfo.stride[1]);
	GstCaps* sourceCaps = gst_video_info_to_caps(&sourceInfo);
//	gst_caps_set_features(sourceCaps, 0, gst_caps_features_from_string(GST_CAPS_FEATURE_MEMORY_DMABUF));
	g_object_set(G_OBJECT(gst.screenSrc), "caps", sourceCaps, nullptr);
	gst_caps_unref(sourceCaps);

	gst_bin_add_many(GST_BIN(gst.pipeline), gst.screenSrc, gst.scaler, gst.encoder, gst.h264parse, gst.muxer, gst.sink, nullptr);
	if (!gst_element_link_many(gst.screenSrc, gst.scaler, gst.encoder, gst.h264parse, nullptr)
	    || !gst_element_link(gst.muxer, gst.sink))
	{
		throw GStreamerException("Could not link pipeline!");
	}

	// muxer only has template pads
	// → create a video input pad that matches the encoder output
	// → muxer will add a video stream to the muxed container
	GstPad* encoderOutput = gst_element_get_static_pad(gst.h264parse, "src");
	gst.muxerInput = gst_element_get_compatible_pad(gst.muxer, encoderOutput, nullptr);
	if (gst_pad_link(encoderOutput, gst.muxerInput) != GST_PAD_LINK_OK)
	{
		throw GStreamerException("Could not link encoder output to new muxer video input pad!");
	}
	gst_object_unref(encoderOutput);

	// start the pipeline
	if (!gst_element_set_state(gst.pipeline, GST_STATE_PLAYING))
	{
		throw GStreamerException("Starting gstreamer pipeline failed!");
	}
//	gst_pipeline_set_auto_flush_bus(GST_PIPELINE(gst.pipeline), true);
}

void releaseGstOutput()
{
	gst_element_set_state(gst.pipeline, GST_STATE_PAUSED);
	if (gst.muxerInput)
	{
		gst_element_release_request_pad(gst.muxer, gst.muxerInput);
		gst_object_unref(gst.muxerInput);
	}
	if (gst.pipeline)
	{
		GstBus* bus = gst_element_get_bus(gst.pipeline);
		pollMessages(bus);
		gst_object_unref(bus);
		gst_element_set_state(gst.pipeline, GST_STATE_NULL);
	}
	gst_object_unref(gst.pipeline);
}

void runMainLoop()
{
	g_main_loop_new(nullptr, false);
	g_main_loop_run(gst.mainLoop);
}
#endif