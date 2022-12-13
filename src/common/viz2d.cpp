#include "viz2d.hpp"

#include "util.hpp"

namespace kb {
void gl_check_error(const std::filesystem::path &file, unsigned int line, const char *expression) {
    int errorCode = glGetError();

    if (errorCode != 0) {
        std::cerr << "GL failed in " << file.filename() << " (" << line << ") : " << "\nExpression:\n   " << expression << "\nError code:\n   " << errorCode << "\n   " << std::endl;
        assert(false);
    }
}

Viz2D::Viz2D(const cv::Size &size, const cv::Size& frameBufferSize, bool offscreen, const string &title, int major, int minor, int samples, bool debug) :
        size_(size), frameBufferSize_(frameBufferSize), offscreen_(offscreen), title_(title), major_(major), minor_(minor), samples_(samples), debug_(debug) {
    assert(frameBufferSize_.width >= size_.width && frameBufferSize_.height >= size_.height);
}

Viz2D::~Viz2D() {
    //don't delete form_. it is autmatically cleaned up by screen_
    if (screen_)
        delete screen_;
    if (writer_)
        delete writer_;
    if (capture_)
        delete capture_;
    if (nvgContext_)
        delete nvgContext_;
    if (clvaContext_)
        delete clvaContext_;
    if (clglContext_)
        delete clglContext_;
    glfwDestroyWindow(getGLFWWindow());
    glfwTerminate();
}

void Viz2D::initialize() {
    assert(glfwInit() == GLFW_TRUE);
    glfwSetErrorCallback(kb::error_callback);

    if (debug_)
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    if (offscreen_)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    glfwSetTime(0);

#ifdef __APPLE__
        glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major_);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor_);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
    glfwWindowHint(GLFW_SAMPLES, samples_);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    /* I figure we don't need double buffering because the texture is our backbuffer
     * But EGL/X11 anyway doesn't support rendering to the front buffer, yet. But on wayland it should work.
     * And I am not sure about vsync on other platforms.
     */
    //    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_FALSE);

    glfwWindow_ = glfwCreateWindow(size_.width, size_.height, title_.c_str(), nullptr, nullptr);
    if (glfwWindow_ == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(-1);
    }
    glfwMakeContextCurrent(getGLFWWindow());

    screen_ = new nanogui::Screen();
    screen_->initialize(getGLFWWindow(), false);
    form_ = new nanogui::FormHelper(screen_);
    this->setSize(size_);

    glfwSetWindowUserPointer(getGLFWWindow(), this);

    glfwSetCursorPosCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, double x, double y) {
        Viz2D *v2d = (Viz2D*) glfwGetWindowUserPointer(glfwWin);
        v2d->screen_->cursor_pos_callback_event(x, y);
    }
    );
    glfwSetMouseButtonCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int button, int action, int modifiers) {
        Viz2D *v2d = (Viz2D*) glfwGetWindowUserPointer(glfwWin);
        v2d->screen_->mouse_button_callback_event(button, action, modifiers);
    }
    );
    glfwSetKeyCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int key, int scancode, int action, int mods) {
        Viz2D *v2d = (Viz2D*) glfwGetWindowUserPointer(glfwWin);
        v2d->screen_->key_callback_event(key, scancode, action, mods);
    }
    );
    glfwSetCharCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, unsigned int codepoint) {
        Viz2D *v2d = (Viz2D*) glfwGetWindowUserPointer(glfwWin);
        v2d->screen_->char_callback_event(codepoint);
    }
    );
    glfwSetDropCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int count, const char **filenames) {
        Viz2D *v2d = (Viz2D*) glfwGetWindowUserPointer(glfwWin);
        v2d->screen_->drop_callback_event(count, filenames);
    }
    );
    glfwSetScrollCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, double x, double y) {
        Viz2D *v2d = (Viz2D*) glfwGetWindowUserPointer(glfwWin);
        v2d->screen_->scroll_callback_event(x, y);
    }
    );


//FIXME resize internal buffers?
//    glfwSetWindowContentScaleCallback(getGLFWWindow(),
//        [](GLFWwindow* glfwWin, float xscale, float yscale) {
//        }
//    );

    glfwSetFramebufferSizeCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int width, int height) {
        Viz2D *v2d = (Viz2D*) glfwGetWindowUserPointer(glfwWin);
        v2d->screen_->resize_callback_event(width, height);
    }
    );

    clglContext_ = new CLGLContext(this->getFrameBufferSize());
    clvaContext_ = new CLVAContext(*clglContext_);
    nvgContext_ = new NanoVGContext(*this, getNVGcontext(), *clglContext_);
}

cv::ogl::Texture2D& Viz2D::texture() {
    return clglContext_->getTexture2D();
}

nanogui::FormHelper* Viz2D::form() {
    return form_;
}

CLGLContext& Viz2D::clgl() {
    return *clglContext_;
}

CLVAContext& Viz2D::clva() {
    return *clvaContext_;
}

NanoVGContext& Viz2D::nvg() {
    return *nvgContext_;
}

cv::Size Viz2D::getVideoFrameSize() {
    return clva().getVideoFrameSize();
}

void Viz2D::setVideoFrameSize(const cv::Size& sz) {
    clva().setVideoFrameSize(sz);
}

void Viz2D::opengl(std::function<void(const cv::Size&)> fn) {
    CLExecScope_t scope(clglContext_->getCLExecContext());
    CLGLContext::GLScope glScope(*clglContext_);
    fn(getFrameBufferSize());
}

void Viz2D::opencl(std::function<void(cv::UMat&)> fn) {
    clgl().opencl(fn);
}

void Viz2D::nanovg(std::function<void(NVGcontext*, const cv::Size&)> fn) {
    nvg().render(fn);
}

bool Viz2D::captureVA() {
    return clva().capture([=, this](cv::UMat &videoFrame) {
        *(this->capture_) >> videoFrame;
    });
}

void Viz2D::writeVA() {
    clva().write([=, this](const cv::UMat &videoFrame) {
        *(this->writer_) << videoFrame;
    });
}

void Viz2D::makeGLFWContextCurrent() {
    glfwMakeContextCurrent(getGLFWWindow());
}

cv::VideoWriter& Viz2D::makeVAWriter(const string &outputFilename, const int fourcc, const float fps, const cv::Size &frameSize, const int vaDeviceIndex) {
    writer_ = new cv::VideoWriter(outputFilename, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('V', 'P', '9', '0'), fps, frameSize, { cv::VIDEOWRITER_PROP_HW_DEVICE, vaDeviceIndex, cv::VIDEOWRITER_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI, cv::VIDEOWRITER_PROP_HW_ACCELERATION_USE_OPENCL, 1 });
    setVideoFrameSize(frameSize);

    if (!clva().hasContext()) {
        clva().copyContext();
    }
    return *writer_;
}

cv::VideoCapture& Viz2D::makeVACapture(const string &intputFilename, const int vaDeviceIndex) {
    //Initialize MJPEG HW decoding using VAAPI
    capture_ = new cv::VideoCapture(intputFilename, cv::CAP_FFMPEG, { cv::CAP_PROP_HW_DEVICE, vaDeviceIndex, cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI, cv::CAP_PROP_HW_ACCELERATION_USE_OPENCL, 1 });
    float w = capture_->get(cv::CAP_PROP_FRAME_WIDTH);
    float h = capture_->get(cv::CAP_PROP_FRAME_HEIGHT);
    setVideoFrameSize(cv::Size(w,h));

    if (!clva().hasContext()) {
        clva().copyContext();
    }

    return *capture_;
}

void Viz2D::clear(const cv::Scalar &rgba) {
    const float &r = rgba[0] / 255.0f;
    const float &g = rgba[1] / 255.0f;
    const float &b = rgba[2] / 255.0f;
    const float &a = rgba[3] / 255.0f;
    GL_CHECK(glClearColor(r, g, b, a));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
}

cv::Size Viz2D::getNativeFrameBufferSize() {
    int w, h;
    glfwGetFramebufferSize(getGLFWWindow(), &w, &h);
    return {w, h};
}

cv::Size Viz2D::getFrameBufferSize() {
    return frameBufferSize_;
}

cv::Size Viz2D::getSize() {
    int w, h;
    glfwGetWindowSize(getGLFWWindow(), &w, &h);
    return {w, h};
}

float Viz2D::getXPixelRatio() {
#if defined(EMSCRIPTEN)
        return emscripten_get_device_pixel_ratio();
#else
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);
    return xscale;
#endif
}

float Viz2D::getYPixelRatio() {
#if defined(EMSCRIPTEN)
        return emscripten_get_device_pixel_ratio();
#else
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);
    return yscale;
#endif
}

void Viz2D::setSize(const cv::Size &sz) {
    screen_->set_size(nanogui::Vector2i(sz.width / getXPixelRatio(), sz.height / getYPixelRatio()));
}

bool Viz2D::isFullscreen() {
    return glfwGetWindowMonitor(getGLFWWindow()) != nullptr;
}

void Viz2D::setFullscreen(bool f) {
    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (f) {
        glfwSetWindowMonitor(getGLFWWindow(), monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(getGLFWWindow(), nullptr, 0, 0, getNativeFrameBufferSize().width, getNativeFrameBufferSize().height, mode->refreshRate);
    }
    setSize(size_);
}

bool Viz2D::isResizable() {
    return glfwGetWindowAttrib(getGLFWWindow(), GLFW_RESIZABLE) == GLFW_TRUE;
}

void Viz2D::setResizable(bool r) {
    glfwWindowHint(GLFW_RESIZABLE, r ? GLFW_TRUE : GLFW_FALSE);
}

bool Viz2D::isVisible() {
    return glfwGetWindowAttrib(getGLFWWindow(), GLFW_VISIBLE) == GLFW_TRUE;
}

void Viz2D::setVisible(bool v) {
    screen_->perform_layout();
    glfwWindowHint(GLFW_VISIBLE, v ? GLFW_TRUE : GLFW_FALSE);
    screen_->set_visible(v);
    setSize(size_);
}

bool Viz2D::isOffscreen() {
    return offscreen_;
}

nanogui::Window* Viz2D::makeWindow(int x, int y, const string &title) {
    return form()->add_window(nanogui::Vector2i(x, y), title);
}

nanogui::Label* Viz2D::makeGroup(const string &label) {
    return form()->add_group(label);
}

nanogui::detail::FormWidget<bool>* Viz2D::makeFormVariable(const string &name, bool &v, const string &tooltip) {
    auto var = form()->add_variable(name, v);
    if (!tooltip.empty())
        var->set_tooltip(tooltip);
    return var;
}

void Viz2D::setUseOpenCL(bool u) {
    clglContext_->getCLExecContext().setUseOpenCL(u);
    clvaContext_->getCLExecContext().setUseOpenCL(u);
    cv::ocl::setUseOpenCL(u);
}

bool Viz2D::display() {
    bool result = true;
    if (!offscreen_) {
        glfwPollEvents();
        screen_->draw_contents();
        clglContext_->blitFrameBufferToScreen(getSize());
        screen_->draw_widgets();
        glfwSwapBuffers(glfwWindow_);
        result = !glfwWindowShouldClose(glfwWindow_);
    }

    return result;
}

bool Viz2D::isClosed() {
    return closed_;

}
void Viz2D::close() {
    setVisible(false);
    closed_ = true;
}

GLFWwindow* Viz2D::getGLFWWindow() {
    return glfwWindow_;
}

NVGcontext* Viz2D::getNVGcontext() {
    return screen_->nvg_context();
}
} /* namespace kb */