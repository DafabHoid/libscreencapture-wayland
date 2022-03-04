/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_VAAPISCALER_HPP
#define SCREENCAPTURE_VAAPISCALER_HPP

#include "libavcommon.hpp"
#include "BlockingRingbuffer.hpp"
#include <thread>

extern "C"
{
#include <libavutil/pixfmt.h>
#include <libavfilter/avfilter.h>
}

namespace ffmpeg
{

using ScalingDoneCallback = std::function<void(AVFrame_Heap)>;

/** Upload and scale frames on the GPU using VAAPI.
 * Frames are converted to the NV12 pixel format during this process.
 * Scaled frames are output by calling the ScalingDoneCallback function. */
class VAAPIScaler
{
	AVFilterGraph* filterGraph;
	AVFilterContext* filterSrcContext;
	AVFilterContext* filterSinkContext;
	const char* const hardwareFrameFilterName;
	ScalingDoneCallback scalingDone;
	BlockingRingbuffer<AVFrame_Heap, 8> scaleQueue;
	std::thread scaleThread;
	std::exception_ptr scaleThreadException;

	void scaleFramesLoop();

public:
	/** Create a new VAAPIScaler with the given source and target dimensions.
	 * @param sourceSize the size of the source frames that should be scaled
	 * @param sourceFormat the pixel format of the sources frames
	 * @param targetSize the target size that the frames should be scaled to
	 * @param drmDevice the DRM device that provides input frames, when inputIsDRMPrime is true
	 * @param vaapiDevice the VAAPI device that should do the scaling
	 * @param inputIsDRMPrime if the input frames are DRM PRIME frames instead of normal memory frames
	 * @param cb the callback to call when a scaled frame becomes available */
	SCW_EXPORT VAAPIScaler(Rect sourceSize, AVPixelFormat sourceFormat, Rect targetSize,
	            AVBufferRef* drmDevice, AVBufferRef* vaapiDevice, bool inputIsDRMPrime, ScalingDoneCallback cb);

	SCW_EXPORT ~VAAPIScaler() noexcept;

	/** Add a frame into the scaling queue. It can be retrieved by dequeueFrame().
	 * If the queue is full, the frame will by silently dropped.
	 * This function is thread-safe. */
	SCW_EXPORT void enqueueFrame(AVFrame_Heap frame);

	struct EndOfQueue {};
	/** Get a frame from the scaling queue. If the queue is empty, this will block until
	 * a frame is added to the queue by enqueueFrame().
	 * When the queue is EOF, return #EndOfQueue.
	 * This function is thread-safe. */
	SCW_EXPORT std::variant<AVFrame_Heap, EndOfQueue> dequeueFrame();

	/** Scale a single frame.
	 * After scaling, the given ScalingDoneCallback is called with the scaled frame.
	 * Ownership of the frame is transferred to the callback.
	 * This function is NOT thread-safe. */
	SCW_EXPORT void scaleFrame(AVFrame& frame);
};

}


#endif //SCREENCAPTURE_VAAPISCALER_HPP
