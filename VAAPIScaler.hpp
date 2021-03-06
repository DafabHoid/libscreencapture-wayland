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

/** Upload and scale frames on the GPU using VAAPI.
 * Frames are converted to the NV12 pixel format during this process.
 * Scaled frames are output by calling the ScalingDoneCallback function. */
class SCW_EXPORT VAAPIScaler
{
	AVFilterGraph* filterGraph;
	AVFilterContext* filterSrcContext;
	AVFilterContext* filterSinkContext;
	const char* const hardwareFrameFilterName;

public:
	using ScalingDoneCallback = std::function<void(AVFrame_Heap)>;

	using CallbackType = ScalingDoneCallback;

	/** Create a new VAAPIScaler with the given source and target dimensions.
	 * @param sourceSize the size of the source frames that should be scaled
	 * @param sourceFormat the pixel format of the sources frames
	 * @param targetSize the target size that the frames should be scaled to
	 * @param drmDevice the DRM device that provides input frames, when inputIsDRMPrime is true
	 * @param vaapiDevice the VAAPI device that should do the scaling
	 * @param inputIsDRMPrime if the input frames are DRM PRIME frames instead of normal memory frames */
	VAAPIScaler(Rect sourceSize, AVPixelFormat sourceFormat, Rect targetSize,
	            AVBufferRef* drmDevice, AVBufferRef* vaapiDevice, bool inputIsDRMPrime);

	VAAPIScaler(VAAPIScaler&&) noexcept;
	VAAPIScaler(const VAAPIScaler&) = delete;
	~VAAPIScaler() noexcept;

	/** Scale a single frame.
	 * After scaling, the given ScalingDoneCallback is called with the scaled frame.
	 * Ownership of the frame is transferred to the callback.
	 * This function is NOT thread-safe. */
	void scaleFrame(AVFrame& frame, const ScalingDoneCallback& scalingDone);

	inline void processFrame(AVFrame& frame, const ScalingDoneCallback& scalingDone)
	{ scaleFrame(frame, scalingDone); }
};


using ThreadedVAAPIScaler = ThreadedWrapper<VAAPIScaler>;

// declare external instantiation for template
extern template class ThreadedWrapper<VAAPIScaler>;

}


#endif //SCREENCAPTURE_VAAPISCALER_HPP
