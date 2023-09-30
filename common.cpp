/*******************************************************************************
   Copyright © 2023 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/

#include "common.hpp"
#include <c_common.h>
#ifdef HAVE_PIPEWIRE_MODLE
#include "PipeWireModule/PipeWireStream.hpp"
#endif
#ifdef HAVE_GSTREAMER_MODULE
#include "GStreamerModule/GstOutput.hpp"
#endif


void screencapture_wayland_init(int* argc, char*** argv)
{
#ifdef HAVE_PIPEWIRE_MODLE
	pw::init(argc, argv);
#endif
#ifdef HAVE_GSTREAMER_MODULE
	gstreamer::init(argc, argv);
#endif
}
void screencapture_wayland_deinit()
{
#ifdef HAVE_PIPEWIRE_MODLE
	pw::deinit();
#endif
#ifdef HAVE_GSTREAMER_MODULE
	gstreamer::deinit();
#endif
}

#include <execinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#ifndef NDEBUG
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
			write(fd, msg, sizeof(msg)-1);
			// write the stack trace, starting from the frame of our calling function
			backtrace_symbols_fd(bt + 1, num - 1, fd);
			close(fd);
		}
	}
}
#endif