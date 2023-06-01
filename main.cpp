#include <pipewire/pipewire.h>
#include "PipeWireModule/PipeWireStream.hpp"
#include "PortalModule/xdg-desktop-portal.hpp"
#include "GStreamerModule/GstOutput.hpp"
#include <cstdio>
#include <unistd.h>

#ifndef NDEBUG
#include <execinfo.h>
#include <fcntl.h>
#include <cstring>
#endif


static void printUsage(const char* argv0)
{
	printf("Usage: %s [-c] -f <output format> -o <output path> -d <hardware device path>\n", argv0);
	puts("\tWhere <hardware device path> is a DRM render node like /dev/dri/renderD128");
	puts("\tWhere <output format> and <output path> can be any string that is recognized by ffmpeg");
}

int main(int argc, char** argv)
{
	int c;
	bool withCursor = false;
	char* hardwareDevicePath = nullptr;
	const char* outputPath = nullptr;
	const char* outputFormat = nullptr;
	while ((c = getopt(argc, argv, "co:f:d:")) != -1)
	{
		switch (c)
		{
			case 'c':
				withCursor = true;
				break;
			case 'o':
				outputPath = optarg;
				break;
			case 'f':
				outputFormat = optarg;
				break;
			case 'd':
				hardwareDevicePath = optarg;
				break;
			case '?':
				fprintf(stderr, "Unrecognized option: '-%c'\n", optopt);
				printUsage(argv[0]);
				return 1;
		}
	}
	if (!outputPath || !outputFormat)
	{
		fprintf(stderr, "Both output path and format must be specified\n");
		printUsage(argv[0]);
		return 1;
	}
	if (!hardwareDevicePath)
	{
		fprintf(stderr, "Missing hardware device path\n");
		printUsage(argv[0]);
		return 1;
	}

	try
	{
		auto cursorMode = withCursor ? CURSOR_MODE_EMBED : CURSOR_MODE_HIDDEN;
		std::optional<portal::SharedScreen> shareInfo = portal::requestPipeWireShare(cursorMode);
		if (!shareInfo)
		{
			printf("User cancelled request\n");
			return 0;
		}

		printf("SharedScreen fd = %d, node = %u\n", shareInfo.value().pipeWireFd, shareInfo.value().pipeWireNode);
		pw_init(&argc, &argv);

		gst_init(&argc, &argv);

		{
			class Stream2FFmpeg : public pw::StreamCallbacks
			{
				std::unique_ptr<gstreamer::GstOutput> gstOutput;
				const char* hardwareDevice;
				const char* outputFormat;
				const char* outputPath;

			public:
				inline Stream2FFmpeg(const char* hardwareDevice, const char* outputFormat, const char* outputPath)
				: hardwareDevice{hardwareDevice},
				  outputFormat{outputFormat},
				  outputPath{outputPath}
				{}

				void streamConnected(pw::Rect dimensions, pw::PixelFormat format, bool isDmaBuf) override
				{
					auto builder = gstreamer::GstOutput::Builder(dimensions, format);
					builder
						.withScaling(common::Rect {1920u, 1080u})
						.withHWDevice(hardwareDevice)
						.withOutputFormat(outputFormat)
						.withOutputPath(outputPath);
					gstOutput = std::make_unique<gstreamer::GstOutput>(builder.build());
				}
				void streamDisconnected() override
				{
					gstOutput.reset();
				}
				void pushMemoryFrame(std::unique_ptr<pw::MemoryFrame> frame) override
				{
					gstOutput->pushFrame(std::move(frame));
				}
				void pushDmaBufFrame(std::unique_ptr<pw::DmaBufFrame> frame) override
				{
				}
			};
			Stream2FFmpeg stream2FFmpeg(hardwareDevicePath, outputFormat, outputPath);
			auto pwStream = pw::PipeWireStream(shareInfo.value(), stream2FFmpeg, false);
			pwStream.runStreamLoop();
		}

		gst_deinit();
		pw_deinit();
		return 0;
	}
	catch (const std::exception& e)
	{
		if (isatty(fileno(stderr)))
			// print exception text in bold red font
			fprintf(stderr, "\x1b[1;31m%s\x1b[0m\n", e.what());
		else
			fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}

/** Dump a stack trace to the file at @param filename.
 * This function does not use the heap, and only opens a file descriptor for the output file. */
void dumpStackTrace(const char* filename) noexcept
{
#ifndef NDEBUG
	void* bt[50];
	int num = backtrace(bt, sizeof(bt)/sizeof(bt[0]));
	// write the stack trace if it was successful and includes more than this function's frame
	if (num > 1)
	{
		int fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
		if (fd < 0)
			perror("Opening trace file failed");
		else
		{
			const char* msg = "Trace for Exception:\n";
			write(fd, msg, strlen(msg));
			// write the stack trace, starting from the frame of our calling function
			backtrace_symbols_fd(bt + 1, num - 1, fd);
			close(fd);
		}
	}
#endif
}