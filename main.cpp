#include <pipewire/pipewire.h>
#include "PipeWireModule/PipeWireStream.hpp"
#include "PortalModule/xdg-desktop-portal.hpp"
#include "FFMPEGModule/FFmpegOutput.hpp"
#include <cstdio>
#include <unistd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <poll.h>

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

// boilerplate for std::visit with lambdas
template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

class FPSCounter
{
	std::chrono::time_point<std::chrono::steady_clock> lastFpsTS;
	int frameCountThisSecond;

public:
	FPSCounter()
	{
		lastFpsTS = std::chrono::steady_clock::now();
		frameCountThisSecond = 0;
	}

	void increment()
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

	sigset_t procMask;
	sigemptyset(&procMask);
	sigaddset(&procMask, SIGINT);
	sigaddset(&procMask, SIGTERM);
	sigprocmask(SIG_BLOCK, &procMask, nullptr);

	int signalFd = signalfd(-1, &procMask, SFD_CLOEXEC);
	if (signalFd == -1)
	{
		perror("creating signalfd failed");
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


		{
			auto pwStream = pw::PipeWireStream(shareInfo.value(), true);

			// this must be declared after and therefore destroyed before pwStream, so that frame processing is stopped
			// and all references to frames from the stream are dropped before pwStream is destroyed.
			std::unique_ptr<ffmpeg::FFmpegOutput> ffmpegOutput;
			FPSCounter fpsCounter;

			bool shouldStop = false;
			while (!shouldStop)
			{
				struct pollfd fds[2];
				fds[0] = {pwStream.getEventPollFd(), POLLIN, 0};
				fds[1] = {signalFd, POLLIN, 0};
				int res = poll(fds, 2, -1);
				if (res == -1)
				{
					if (errno == EAGAIN || errno == EINTR)
						continue;
					perror("poll failed");
					return 1;
				}
				if (fds[1].revents & POLLIN)
				{
					signalfd_siginfo siginfo;
					read(signalFd, &siginfo, sizeof(siginfo));
					if (siginfo.ssi_signo == SIGINT || siginfo.ssi_signo == SIGTERM)
						shouldStop = true;
				}
				if (!(fds[0].revents & POLLIN))
					continue;
				auto ev = pwStream.nextEvent();
				if (ev)
				{
					// call lambda function appropriate for the type of *ev
					std::visit(overloaded{
							[&] (pw::event::Connected& e)
							{
								auto builder = ffmpeg::FFmpegOutput::Builder(e.dimensions, e.format, e.isDmaBuf);
								builder
										.withScaling(common::Rect{1920u, 1080u})
										.withHWDevice(hardwareDevicePath)
										.withOutputFormat(outputFormat)
										.withOutputPath(outputPath);
								ffmpegOutput = std::make_unique<ffmpeg::FFmpegOutput>(builder.build());
								// restart the fps counter
								fpsCounter = FPSCounter();
							},
							[&] (pw::event::Disconnected&)
							{
								shouldStop = true;
							},
							[&] (pw::event::MemoryFrameReceived& e)
							{
								auto avFrame = ffmpeg::wrapInAVFrame(std::move(e.frame));
								ffmpegOutput->pushFrame(ffmpeg::AVFrame_Heap(avFrame));
								fpsCounter.increment();
							},
							[&] (pw::event::DmaBufFrameReceived& e)
							{
								auto avFrame = ffmpeg::wrapInAVFrame(std::move(e.frame));
								ffmpegOutput->pushFrame(ffmpeg::AVFrame_Heap(avFrame));
								fpsCounter.increment();
							}
					}, *ev);
				}
			}
		}

		pw_deinit();
		close(signalFd);
		return 0;
	}
	catch (const std::exception& e)
	{
		close(signalFd);
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