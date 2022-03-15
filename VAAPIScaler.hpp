/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#ifndef SCREENCAPTURE_VAAPISCALER_HPP
#define SCREENCAPTURE_VAAPISCALER_HPP

#include "libavcommon.hpp"
#include "ThreadedWrapper.hpp"

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

	SCW_EXPORT VAAPIScaler(VAAPIScaler&&) noexcept;
	           VAAPIScaler(const VAAPIScaler&) = delete;
	SCW_EXPORT ~VAAPIScaler() noexcept;

	/** Scale a single frame.
	 * After scaling, the given ScalingDoneCallback is called with the scaled frame.
	 * Ownership of the frame is transferred to the callback.
	 * This function is NOT thread-safe. */
	SCW_EXPORT void scaleFrame(AVFrame& frame);
};


using ThreadedVAAPIScaler = ThreadedWrapper<VAAPIScaler, &VAAPIScaler::scaleFrame>;

// declare external instantiation for template
extern template class ThreadedWrapper<VAAPIScaler, &VAAPIScaler::scaleFrame>;

}


#endif //SCREENCAPTURE_VAAPISCALER_HPP
