#include <pipewire/pipewire.h>
#include "PipeWireModule/PipeWireStream.hpp"
#include "PortalModule/xdg-desktop-portal.hpp"
#include "FFMPEGModule/FFmpegOutput.hpp"
#include <cstdio>
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <atomic>

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

static std::atomic_bool shouldStop = false;

static void quitPwStreamOnTerminateSignal(pw::PipeWireStream& pwStream)
{
	sigset_t signalsToWaitFor;
	sigemptyset(&signalsToWaitFor);
	sigaddset(&signalsToWaitFor, SIGINT);
	sigaddset(&signalsToWaitFor, SIGTERM);
	int sig = 0;
	sigwait(&signalsToWaitFor, &sig);

	printf("Stopping stream...\n");
	shouldStop = true;
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

	// Block both of these signals, so they don't get delivered except to to a separate thread created later
	sigset_t procset;
	sigemptyset(&procset);
	sigaddset(&procset, SIGINT);
	sigaddset(&procset, SIGTERM);
	sigprocmask(SIG_BLOCK, &procset, nullptr);

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


		{
			class Stream2FFmpeg : public pw::StreamCallbacks
			{
				std::unique_ptr<ffmpeg::FFmpegOutput> ffmpegOutput;
				const char* hardwareDevice;
				const char* outputFormat;
				const char* outputPath;
				std::chrono::time_point<std::chrono::steady_clock> lastFpsTS;
				int frameCountThisSecond;

			public:
				inline Stream2FFmpeg(const char* hardwareDevice, const char* outputFormat, const char* outputPath)
				: hardwareDevice{hardwareDevice},
				  outputFormat{outputFormat},
				  outputPath{outputPath}
				{}

				void processEvent(pw::event::Event e) override
				{
					// delegate event to type-matching operator method
					std::visit(*this, e);
				}

				void operator()(pw::event::Connected& e)
				{
					auto builder = ffmpeg::FFmpegOutput::Builder(e.dimensions, e.format, e.isDmaBuf);
					builder
						.withScaling(common::Rect {1920u, 1080u})
						.withHWDevice(hardwareDevice)
						.withOutputFormat(outputFormat)
						.withOutputPath(outputPath);
					ffmpegOutput = std::make_unique<ffmpeg::FFmpegOutput>(builder.build());
					lastFpsTS = std::chrono::steady_clock::now();
					frameCountThisSecond = 0;
				}
				void operator()(pw::event::Disconnected&)
				{
					ffmpegOutput.reset();
				}
				void operator()(pw::event::MemoryFrameReceived& e)
				{
					auto avFrame = ffmpeg::wrapInAVFrame(std::move(e.frame));
					ffmpegOutput->pushFrame(ffmpeg::AVFrame_Heap (avFrame));
					updateFpsCounter();
				}
				void operator()(pw::event::DmaBufFrameReceived& e)
				{
					auto avFrame = ffmpeg::wrapInAVFrame(std::move(e.frame));
					ffmpegOutput->pushFrame(ffmpeg::AVFrame_Heap (avFrame));
					updateFpsCounter();
				}
				void updateFpsCounter()
				{
					auto now = std::chrono::steady_clock::now();
					if (now - lastFpsTS >= std::chrono::seconds(1))
					{
						printf("fps: %d\n", frameCountThisSecond);
						lastFpsTS = now;
						frameCountThisSecond = 0;
					}
					++frameCountThisSecond;
				}
			};
			Stream2FFmpeg stream2FFmpeg(hardwareDevicePath, outputFormat, outputPath);
			auto pwStream = pw::PipeWireStream(shareInfo.value(), true);

			// start thread to wait for a terminate signal for a graceful shutdown
			std::thread signalHandlerThread([&pwStream]() { quitPwStreamOnTerminateSignal(pwStream); });

			while (!shouldStop)
			try
			{
				auto ev = pwStream.pollEvent(std::chrono::seconds(-1));
				if (ev)
				{
					if (std::holds_alternative<pw::event::Disconnected>(*ev))
						shouldStop = true;
					stream2FFmpeg.processEvent(std::move(*ev));
				}
			} catch(...) {
				kill(0, SIGTERM);
				signalHandlerThread.join();
				throw;
			}
			// raise signal ourselves to unblock the thread if it is still running
			kill(0, SIGTERM);
			signalHandlerThread.join();
		}

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

#ifndef NDEBUG
/** Dump a stack trace to the file at @param filename.
 * This function does not use the heap, and only opens a file descriptor for the output file. */
void dumpStackTrace(const char* filename) noexcept
{
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
}
#endif