#include <pipewire/pipewire.h>
#include "PipeWireStream.hpp"
#include "xdg-desktop-portal.hpp"
#include "FFmpegOutput.hpp"
#include <cstdio>
#include <unistd.h>

#ifndef NDEBUG
#include <execinfo.h>
#include <fcntl.h>
#include <cstring>
#endif


int main(int argc, char** argv)
{
	try
	{
		std::optional<SharedScreen> shareInfo = portal::requestPipeWireShare(CURSOR_MODE_EMBED);
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
			public:
				void streamConnected(Rect dimensions, PixelFormat format, bool isDmaBuf) override
				{
					ffmpegOutput = std::make_unique<ffmpeg::FFmpegOutput>(dimensions, format, isDmaBuf);
				}
				void streamDisconnected() override
				{
					ffmpegOutput.reset();
				}
				void pushMemoryFrame(std::unique_ptr<MemoryFrame> frame) override
				{
					ffmpegOutput->pushFrame(ffmpeg::AVFrame_Heap(ffmpeg::wrapInAVFrame(std::move(frame))));
				}
				void pushDmaBufFrame(std::unique_ptr<DmaBufFrame> frame) override
				{
					ffmpegOutput->pushFrame(ffmpeg::AVFrame_Heap(ffmpeg::wrapInAVFrame(std::move(frame))));
				}
			};
			Stream2FFmpeg stream2FFmpeg;
			auto pwStream = pw::PipeWireStream(shareInfo.value(), stream2FFmpeg);
			pwStream.runStreamLoop();
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