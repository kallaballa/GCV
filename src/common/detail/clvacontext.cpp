#include "clvacontext.hpp"

#include "../viz2d.hpp"

namespace kb {
namespace viz2d {
namespace detail {

CLVAContext::CLVAContext(CLGLContext &clglContext) :
        clglContext_(clglContext) {
}

void CLVAContext::setVideoFrameSize(const cv::Size& sz) {
    if(videoFrameSize_ != cv::Size(0,0))
        assert(videoFrameSize_ == sz || "Input and output video sizes don't match");

    videoFrameSize_ = sz;
}

cv::Size CLVAContext::getVideoFrameSize() {
    assert(videoFrameSize_ == cv::Size(0,0) || "Video frame size not initialized");
    return videoFrameSize_;
}

bool CLVAContext::capture(std::function<void(cv::UMat&)> fn) {
    {
        if(!context_ .empty()) {
#ifndef __EMSCRIPTEN__
            CLExecScope_t scope(context_);
#endif
            fn(videoFrame_);
            videoFrameSize_ = videoFrame_.size();
        } else {
            fn(videoFrame_);
            videoFrameSize_ = videoFrame_.size();
        }
    }
    {
#ifndef __EMSCRIPTEN__
        CLExecScope_t scope(clglContext_.getCLExecContext());
#endif
        CLGLContext::GLScope glScope(clglContext_);
        CLGLContext::FrameBufferScope fbScope(clglContext_, frameBuffer_);
        if (videoFrame_.empty())
            return false;

        cv::Size fbSize = clglContext_.getSize();
        cv::resize(videoFrame_, rgbBuffer_, fbSize);
        cv::cvtColor(rgbBuffer_, frameBuffer_, cv::COLOR_RGB2BGRA);

        assert(frameBuffer_.size() == fbSize);
    }
    return true;
}

void CLVAContext::write(std::function<void(const cv::UMat&)> fn) {
    {
#ifndef __EMSCRIPTEN__
        CLExecScope_t scope(clglContext_.getCLExecContext());
#endif
        CLGLContext::GLScope glScope(clglContext_);
        CLGLContext::FrameBufferScope fbScope(clglContext_, frameBuffer_);

        cv::cvtColor(frameBuffer_, rgbBuffer_, cv::COLOR_BGRA2RGB);
        cv::resize(rgbBuffer_, videoFrame_, videoFrameSize_);
    }
    assert(videoFrame_.size() == videoFrameSize_);
    {
#ifndef __EMSCRIPTEN__
        CLExecScope_t scope(context_);
#endif
        fn(videoFrame_);
    }
}

bool CLVAContext::hasContext() {
    return !context_.empty();
}

void CLVAContext::copyContext() {
#ifndef __EMSCRIPTEN__
    context_ = CLExecContext_t::getCurrent();
#endif
}

CLExecContext_t CLVAContext::getCLExecContext() {
    return context_;
}
}
}
}
