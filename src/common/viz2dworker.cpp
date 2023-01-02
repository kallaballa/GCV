#include "viz2dworker.hpp"

#include "detail/clglcontext.hpp"
#include "detail/clvacontext.hpp"
#include "detail/nanovgcontext.hpp"

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

namespace kb {
namespace viz2d {
namespace detail {

std::map<string, cv::UMat> MemoryPool::sharedMap_;
std::map<string, void*> MemoryPool::sharedVarMap_;

void gl_check_error(const std::filesystem::path &file, unsigned int line, const char *expression) {
    int errorCode = glGetError();

    if (errorCode != 0) {
        std::cerr << "GL failed in " << file.filename() << " (" << line << ") : " << "\nExpression:\n   " << expression << "\nError code:\n   " << errorCode << "\n   " << std::endl;
        assert(false);
    }
}

void error_callback(int error, const char *description) {
    fprintf(stderr, "GLFW Error: %s\n", description);
}
}

template <typename T> void find_widgets(nanogui::Widget* parent, std::vector<T>& widgets) {
    T w;
    for(auto* child: parent->children()) {
        find_widgets(child, widgets);
        if((w = dynamic_cast<T>(child)) != nullptr) {
            widgets.push_back(w);
        }
    }
}

bool contains_absolute(nanogui::Widget* w, const nanogui::Vector2i &p) {
    nanogui::Vector2i d = p - w->absolute_position();
    return d.x() >= 0 && d.y() >= 0 &&
           d.x() < w->size().x() && d.y() < w->size().y();
}

cv::Scalar color_convert(const cv::Scalar& src, cv::ColorConversionCodes code) {
    cv::Mat tmpIn(1,1,CV_8UC3);
    cv::Mat tmpOut(1,1,CV_8UC3);

    tmpIn.at<cv::Vec3b>(0,0) = cv::Vec3b(src[0], src[1], src[2]);
    cvtColor(tmpIn, tmpOut, code);
    const cv::Vec3b& vdst = tmpOut.at<cv::Vec3b>(0,0);
    cv::Scalar dst(vdst[0],vdst[1],vdst[2], src[3]);
    return dst;
}

std::function<bool(Viz2DWindow*, Viz2DWindow*)> Viz2DWindow::viz2DWin_Xcomparator([](Viz2DWindow* lhs, Viz2DWindow* rhs){ return lhs != nullptr && rhs != nullptr && lhs->position()[0] < rhs->position()[0]; });
std::set<Viz2DWindow*, decltype(Viz2DWindow::viz2DWin_Xcomparator)> Viz2DWindow::all_windows_xsorted_(viz2DWin_Xcomparator);

Viz2DWindow::Viz2DWindow(nanogui::Screen *screen, int x, int y, const string &title) :
        Window(screen, title), screen_(screen), lastDragPos_(x, y) {
    all_windows_xsorted_.insert(this);
    oldLayout_ = new nanogui::AdvancedGridLayout( { 10, 0, 10, 0 }, { });
    oldLayout_->set_margin(10);
    oldLayout_->set_col_stretch(2, 1);
    this->set_position( { x, y });
    this->set_layout(oldLayout_);
    this->set_visible(true);

    minBtn_ = this->button_panel()->add<nanogui::Button>("_");
    maxBtn_ = this->button_panel()->add<nanogui::Button>("+");
    newLayout_ = new nanogui::AdvancedGridLayout( { 10, 0, 10, 0 }, { });

    maxBtn_->set_visible(false);

    maxBtn_->set_callback([&, this]() {
        this->minBtn_->set_visible(true);
        this->maxBtn_->set_visible(false);

        for (auto *child : this->children()) {
            child->set_visible(true);
        }

        this->set_layout(oldLayout_);
        this->set_position(maximizedPos_);
        this->screen_->perform_layout();
        this->minimized_ = false;
    });

    minBtn_->set_callback([&, this]() {
        this->minBtn_->set_visible(false);
        this->maxBtn_->set_visible(true);

        for (auto *child : this->children()) {
            child->set_visible(false);
        }
        this->set_size( { 0, 0 });
        this->set_layout(newLayout_);
        this->screen_->perform_layout();
        int gap = 0;
        int x = 0;
        int w = width();
        int lastX = 0;
        this->maximizedPos_ = this->position();

        for (Viz2DWindow* win : all_windows_xsorted_) {
            if(win != this && win->isMinimized()) {
                x = win->position()[0];
                gap = lastX + x;
                if(gap >= w) {
                    this->set_position({lastX, screen_->height() - this->height()});
                    break;
                }
                lastX = x + win->width() + 1;
            }
        }
        if(gap < w) {
            this->set_position({lastX, screen_->height() - this->height()});
        }
        this->minimized_ = true;
    });
}

Viz2DWindow::~Viz2DWindow() {
    all_windows_xsorted_.erase(this);
}

bool Viz2DWindow::isMinimized() {
    return minimized_;
}

bool Viz2DWindow::mouse_drag_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int mods) {
    if (m_drag && (button & (1 << GLFW_MOUSE_BUTTON_1)) != 0) {
        if(maxBtn_->visible()) {
            for (auto *win : all_windows_xsorted_) {
                if (win != this) {
                    if (win->contains(this->position())
                            || win->contains( { this->position()[0] + this->size()[0], this->position()[1] + this->size()[1] })
                            || win->contains( { this->position()[0], this->position()[1] + this->size()[1] })
                            || win->contains( { this->position()[0] + this->size()[0], this->position()[1] })
                            || this->contains(win->position())
                            || this->contains( { win->position()[0] + win->size()[0], win->position()[1] + win->size()[1] })
                            || this->contains( { win->position()[0], win->position()[1] + win->size()[1] })
                            || this->contains( { win->position()[0] + win->size()[0], win->position()[1] })) {
                        this->set_position(lastDragPos_);
                        return true;
                    }
                }
            }
        }
        lastDragPos_ = m_pos;
        bool result = nanogui::Window::mouse_drag_event(p, rel, button, mods);

        return result;
    }
    return false;
}

Viz2DWorker::Viz2DWorker(const cv::Size &size, const cv::Size& frameBufferSize, bool offscreen, const string &title, int major, int minor, int samples, bool debug) :
        initialSize_(size), frameBufferSize_(frameBufferSize), viewport_(0, 0, frameBufferSize.width, frameBufferSize.height), scale_(1), mousePos_(0,0), offscreen_(offscreen), stretch_(false), title_(title), major_(major), minor_(minor), samples_(samples), debug_(debug) {
    assert(frameBufferSize_.width >= initialSize_.width && frameBufferSize_.height >= initialSize_.height);

    initializeWindowing();
}

Viz2DWorker::~Viz2DWorker() {
    //don't delete form_. it is autmatically cleaned up by the base class (nanogui::Screen)
    if(screen_)
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
}

bool Viz2DWorker::initializeWindowing() {
    if(glfwInit() != GLFW_TRUE)
        return false;

    glfwSetErrorCallback(kb::viz2d::error_callback);

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
#elif __EMSCRIPTEN__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API) ;
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

    glfwWindow_ = glfwCreateWindow(initialSize_.width, initialSize_.height, title_.c_str(), nullptr, nullptr);
    if (glfwWindow_ == NULL) {
        return false;
    }
    glfwMakeContextCurrent(getGLFWWindow());

    screen_ = new nanogui::Screen();
    screen().initialize(getGLFWWindow(), false);
    form_ = new nanogui::FormHelper(&screen());

    this->setWindowSize(initialSize_);

    glfwSetWindowUserPointer(getGLFWWindow(), this);

    glfwSetCursorPosCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, double x, double y) {
        Viz2DWorker* v2d = reinterpret_cast<Viz2DWorker*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().cursor_pos_callback_event(x, y);
        auto cursor = v2d->getMousePosition();
        auto diff = cursor - cv::Vec2f(x, y);
        if(v2d->isMouseDrag()) {
            v2d->pan(diff[0], -diff[1]);
        }
        v2d->setMousePosition(x, y);
    }
    );
    glfwSetMouseButtonCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int button, int action, int modifiers) {
        Viz2DWorker* v2d = reinterpret_cast<Viz2DWorker*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().mouse_button_callback_event(button, action, modifiers);
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            v2d->setMouseDrag(action == GLFW_PRESS);
        }
    }
    );
    glfwSetKeyCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int key, int scancode, int action, int mods) {
        Viz2DWorker* v2d = reinterpret_cast<Viz2DWorker*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().key_callback_event(key, scancode, action, mods);
    }
    );
    glfwSetCharCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, unsigned int codepoint) {
        Viz2DWorker* v2d = reinterpret_cast<Viz2DWorker*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().char_callback_event(codepoint);
    }
    );
    glfwSetDropCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int count, const char **filenames) {
        Viz2DWorker* v2d = reinterpret_cast<Viz2DWorker*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().drop_callback_event(count, filenames);
    }
    );
    glfwSetScrollCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, double x, double y) {
        Viz2DWorker* v2d = reinterpret_cast<Viz2DWorker*>(glfwGetWindowUserPointer(glfwWin));
        std::vector<nanogui::Widget*> widgets;
        find_widgets(&v2d->screen(), widgets);
        for(auto* w : widgets) {
            auto mousePos = nanogui::Vector2i(v2d->getMousePosition()[0] / v2d->getXPixelRatio(), v2d->getMousePosition()[1] / v2d->getYPixelRatio());
            if(contains_absolute(w, mousePos)) {
                v2d->screen().scroll_callback_event(x, y);
                return;
            }
        }

        v2d->zoom(y < 0 ? 1.1 : 0.9);
    }
    );

//FIXME resize internal buffers?
//    glfwSetWindowContentScaleCallback(getGLFWWindow(),
//        [](GLFWwindow* glfwWin, float xscale, float yscale) {
//        }
//    );

    glfwSetFramebufferSizeCallback(getGLFWWindow(), [](GLFWwindow *glfwWin, int width, int height) {
        Viz2DWorker* v2d = reinterpret_cast<Viz2DWorker*>(glfwGetWindowUserPointer(glfwWin));
        v2d->screen().resize_callback_event(width, height);
    }
    );

    clglContext_ = new detail::CLGLContext(this->getFrameBufferSize());
    clvaContext_ = new detail::CLVAContext(*clglContext_);
    nvgContext_ = new detail::NanoVGContext(*this, getNVGcontext(), *clglContext_);
    return true;
}

cv::ogl::Texture2D& Viz2DWorker::texture() {
    return clglContext_->getTexture2D();
}

nanogui::FormHelper* Viz2DWorker::form() {
    return form_;
}

bool Viz2DWorker::keyboard_event(int key, int scancode, int action, int modifiers) {
    if (screen().keyboard_event(key, scancode, action, modifiers))
        return true;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        setOffscreen(!isOffscreen());
        return true;
    } else if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        auto children = screen().children();
        for(auto* child : children) {
            child->set_visible(!child->visible());
        }

        return true;
    }
    return false;
}

CLGLContext& Viz2DWorker::clgl() {
    assert(clglContext_ != nullptr);
    return *clglContext_;
}

CLVAContext& Viz2DWorker::clva() {
    assert(clvaContext_ != nullptr);
    return *clvaContext_;
}

NanoVGContext& Viz2DWorker::nvg() {
    assert(nvgContext_ != nullptr);
    return *nvgContext_;
}

nanogui::Screen& Viz2DWorker::screen() {
    assert(screen_ != nullptr);
    return *screen_;
}

cv::Size Viz2DWorker::getVideoFrameSize() {
    return clva().getVideoFrameSize();
}

void Viz2DWorker::setVideoFrameSize(const cv::Size& sz) {
    clva().setVideoFrameSize(sz);
}

Task Viz2DWorker::gl(const string& name, std::function<void(Storage&, const cv::Size&)> fn) {
    return { "gl:" + name, this->frameCnt_, true, [=, this]() {
        auto fbSize = getFrameBufferSize();
#ifndef __EMSCRIPTEN__
        detail::CLExecScope_t scope(clgl().getCLExecContext());
#endif
        detail::CLGLContext::GLScope glScope(clgl());
        fn(this->storage(), fbSize);
    }, this };
}

Task Viz2DWorker::cl(const string& name, std::function<void(Storage&)> fn) {
    return { "cl:" + name, this->frameCnt_, false, [=, this]() {
#ifndef __EMSCRIPTEN__
        detail::CLExecScope_t scope(clgl().getCLExecContext());
#endif
        fn(this->storage());
    }, this };
}

Task Viz2DWorker::cpu(const string& name, std::function<void(Storage&)> fn) {
    return { "cpu:" + name, this->frameCnt_, false, [=, this](){
        fn(this->storage());
    }, this};
}

Task Viz2DWorker::clgl(const string& name, std::function<void(Storage&, cv::UMat&)> fn) {
    return { "clgl:" + name, this->frameCnt_, true, [=, this]() {
        clgl().execute(this->storage(), fn);
    }, this};
}

Task Viz2DWorker::nvg(const string& name, std::function<void(Storage&, const cv::Size&)> fn) {
    return { "nvg:" + name, this->frameCnt_, true, [=, this]() {
        nvg().render(this->storage(),fn);
    }, this};
}

bool Viz2DWorker::capture() {
    return clva().capture([=, this](cv::UMat &videoFrame) {
        *(this->capture_) >> videoFrame;
    });
}

bool Viz2DWorker::capture(std::function<void(cv::UMat&)> fn) {
    bool res = clva().capture(fn);
    ++this->frameCnt_;
    return res;
}

bool Viz2DWorker::hasCapture() {
    return this->capture_ != nullptr;
}

void Viz2DWorker::write() {
    clva().write([=, this](const cv::UMat &videoFrame) {
        *(this->writer_) << videoFrame;
    });
}

void Viz2DWorker::write(std::function<void(const cv::UMat&)> fn) {
    clva().write(fn);
}

bool Viz2DWorker::hasWriter() {
    return this->writer_ != nullptr;
}

void Viz2DWorker::makeCurrent() {
    glfwMakeContextCurrent(getGLFWWindow());
}

void Viz2DWorker::makeNonCurrent() {
    glfwMakeContextCurrent(nullptr);
}
#ifndef __EMSCRIPTEN__
cv::VideoWriter& Viz2DWorker::makeVAWriter(const string &outputFilename, const int fourcc, const float fps, const cv::Size &frameSize, const int vaDeviceIndex) {
    writerPath_ = outputFilename;
    vaWriterDeviceIndex_ = vaDeviceIndex;

    writer_ = new cv::VideoWriter(outputFilename, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('V', 'P', '9', '0'), fps, frameSize, { cv::VIDEOWRITER_PROP_HW_DEVICE, vaDeviceIndex, cv::VIDEOWRITER_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI, cv::VIDEOWRITER_PROP_HW_ACCELERATION_USE_OPENCL, 1 });
    setVideoFrameSize(frameSize);

    if (!clva().hasContext()) {
        clva().copyContext();
    }
    return *writer_;
}

cv::VideoCapture& Viz2DWorker::makeVACapture(const string &inputFilename, const int vaDeviceIndex) {
    capturePath_ = inputFilename;
    vaCaptureDeviceIndex_ = vaDeviceIndex;
    capture_ = new cv::VideoCapture(inputFilename, cv::CAP_FFMPEG, { cv::CAP_PROP_HW_DEVICE, vaDeviceIndex, cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI, cv::CAP_PROP_HW_ACCELERATION_USE_OPENCL, 1 });
    float w = capture_->get(cv::CAP_PROP_FRAME_WIDTH);
    float h = capture_->get(cv::CAP_PROP_FRAME_HEIGHT);
    setVideoFrameSize(cv::Size(w,h));

    if (!clva().hasContext()) {
        clva().copyContext();
    }

    return *capture_;
}

cv::VideoWriter& Viz2DWorker::makeWriter(const string &outputFilename, const int fourcc, const float fps, const cv::Size &frameSize) {
    writerPath_ = outputFilename;
    writer_ = new cv::VideoWriter(outputFilename, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('V', 'P', '9', '0'), fps, frameSize, {});
    setVideoFrameSize(frameSize);

    return *writer_;
}

cv::VideoCapture& Viz2DWorker::makeCapture(const string &inputFilename) {
    capturePath_ = inputFilename;
    capture_ = new cv::VideoCapture(inputFilename, cv::CAP_FFMPEG, {});
    float w = capture_->get(cv::CAP_PROP_FRAME_WIDTH);
    float h = capture_->get(cv::CAP_PROP_FRAME_HEIGHT);
    setVideoFrameSize(cv::Size(w,h));

    return *capture_;
}
#endif

void Viz2DWorker::setMouseDrag(bool d) {
    mouseDrag_ = d;
}

bool Viz2DWorker::isMouseDrag() {
    return mouseDrag_;
}

void Viz2DWorker::pan(int x, int y) {
    viewport_.x += x * scale_;
    viewport_.y += y * scale_;
}

void Viz2DWorker::zoom(float factor) {
    if(scale_ == 1 && viewport_.x == 0 && viewport_.y == 0 && factor > 1)
        return;

    double oldScale = scale_;
    double origW = getFrameBufferSize().width;
    double origH = getFrameBufferSize().height;

    scale_ *= factor;
    if(scale_ <= 0.025) {
        scale_ = 0.025;
        return;
    } else if(scale_ > 1) {
        scale_ = 1;
        viewport_.width = origW;
        viewport_.height = origH;
        if(factor > 1) {
            viewport_.x += log10(((viewport_.x * (1.0 - factor)) / viewport_.width) * 9 + 1.0) * viewport_.width;
            viewport_.y += log10(((viewport_.y * (1.0 - factor)) / viewport_.height) * 9 + 1.0) * viewport_.height;
        } else {
            viewport_.x += log10(((-viewport_.x * (1.0 - factor)) / viewport_.width) * 9 + 1.0) * viewport_.width;
            viewport_.y += log10(((-viewport_.y * (1.0 - factor)) / viewport_.height) * 9 + 1.0) * viewport_.height;
        }
        return;
    }

    cv::Vec2f offset;
    double oldW = (origW * oldScale);
    double oldH = (origH * oldScale);
    viewport_.width = std::min(scale_ * origW, origW);
    viewport_.height = std::min(scale_ * origH, origH);

    float delta_x;
    float delta_y;

    if(factor < 1.0) {
        offset = cv::Vec2f(viewport_.x, viewport_.y) - cv::Vec2f(mousePos_[0], origH - mousePos_[1]);
        delta_x = offset[0] / oldW;
        delta_y = offset[1] / oldH;
    } else {
        offset = cv::Vec2f(viewport_.x - (viewport_.width / 2.0), viewport_.y - (viewport_.height / 2.0)) - cv::Vec2f(viewport_.x, viewport_.y);
        delta_x = offset[0] / oldW;
        delta_y = offset[1] / oldH;
    }

    float x_offset;
    float y_offset;
        x_offset = delta_x * (viewport_.width - oldW);
        y_offset = delta_y * (viewport_.height - oldH);

    if (factor < 1.0) {
        viewport_.x += x_offset;
        viewport_.y += y_offset;
    } else {
        viewport_.x += x_offset;
        viewport_.y += y_offset;
    }
}

cv::Vec2f Viz2DWorker::getPosition() {
    int x, y;
    glfwGetWindowPos(getGLFWWindow(), &x, &y);
    return {float(x), float(y)};
}

cv::Vec2f Viz2DWorker::getMousePosition() {
    return mousePos_;
}

void Viz2DWorker::setMousePosition(int x, int y) {
    mousePos_ = {float(x), float(y)};
}

float Viz2DWorker::getScale() {
    return scale_;
}

cv::Rect Viz2DWorker::getViewport() {
    return viewport_;
}

cv::Size Viz2DWorker::getNativeFrameBufferSize() {
    int w, h;
    glfwGetFramebufferSize(getGLFWWindow(), &w, &h);
    return {w, h};
}

cv::Size Viz2DWorker::getFrameBufferSize() {
    return frameBufferSize_;
}

cv::Size Viz2DWorker::getWindowSize() {
    int w, h;
    glfwGetWindowSize(getGLFWWindow(), &w, &h);
    return {w, h};
}

cv::Size Viz2DWorker::getInitialSize() {
    return initialSize_;
}

float Viz2DWorker::getXPixelRatio() {
#ifdef __EMSCRIPTEN__
    return emscripten_get_device_pixel_ratio();
#else
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);
    return xscale;
#endif
}

float Viz2DWorker::getYPixelRatio() {
#ifdef __EMSCRIPTEN__
    return emscripten_get_device_pixel_ratio();
#else
    float xscale, yscale;
    glfwGetWindowContentScale(getGLFWWindow(), &xscale, &yscale);
    return yscale;
#endif
}

void Viz2DWorker::setWindowSize(const cv::Size &sz) {
    screen().set_size(nanogui::Vector2i(sz.width / getXPixelRatio(), sz.height / getYPixelRatio()));
}

bool Viz2DWorker::isFullscreen() {
    return glfwGetWindowMonitor(getGLFWWindow()) != nullptr;
}

void Viz2DWorker::setFullscreen(bool f) {
    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (f) {
        glfwSetWindowMonitor(getGLFWWindow(), monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        setWindowSize(getNativeFrameBufferSize());
    } else {
        glfwSetWindowMonitor(getGLFWWindow(), nullptr, 0, 0, getInitialSize().width, getInitialSize().height, 0);
        setWindowSize(getInitialSize());
    }
}

bool Viz2DWorker::isResizable() {
    return glfwGetWindowAttrib(getGLFWWindow(), GLFW_RESIZABLE) == GLFW_TRUE;
}

void Viz2DWorker::setResizable(bool r) {
    glfwWindowHint(GLFW_RESIZABLE, r ? GLFW_TRUE : GLFW_FALSE);
}

bool Viz2DWorker::isVisible() {
    return glfwGetWindowAttrib(getGLFWWindow(), GLFW_VISIBLE) == GLFW_TRUE;
}

void Viz2DWorker::setVisible(bool v) {
    glfwWindowHint(GLFW_VISIBLE, v ? GLFW_TRUE : GLFW_FALSE);
    screen().set_visible(v);
//    setSize(size_);
    screen().perform_layout();
}

bool Viz2DWorker::isOffscreen() {
    return offscreen_;
}

void Viz2DWorker::setOffscreen(bool o) {
    offscreen_ = o;
    setVisible(!o);
}

void Viz2DWorker::setStretching(bool s) {
    stretch_ = s;
}

bool Viz2DWorker::isStretching() {
    return stretch_;
}

Viz2DWindow* Viz2DWorker::addWindow(int x, int y, const string &title) {
    auto* win = new kb::viz2d::Viz2DWindow(&screen(), x, y, title);
    this->form()->set_window(win);
    return win;
}

nanogui::Label* Viz2DWorker::addGroup(const string &label) {
    return form()->add_group(label);
}

nanogui::detail::FormWidget<bool>* Viz2DWorker::addVariable(const string &name, bool &v, const string &tooltip, bool visible, bool enabled) {
    auto var = form()->add_variable(name, v);
    var->set_enabled(enabled);
    var->set_visible(visible);
    if (!tooltip.empty())
        var->set_tooltip(tooltip);
    return var;
}

nanogui::detail::FormWidget<nanogui::Color>* Viz2DWorker::addColorPicker(const string& label, nanogui::Color& color, const string& tooltip, bool visible, bool enabled) {
    auto* colorPicker = form()->add_variable(label, color);
    colorPicker->set_enabled(enabled);
    colorPicker->set_visible(visible);
    if (!tooltip.empty())
    colorPicker->set_tooltip(tooltip);

    return colorPicker;
}

nanogui::Button* Viz2DWorker::addButton(const string& caption, std::function<void()> fn) {
    return this->form()->add_button(caption, fn);
}
//bool Viz2DWorker::isAccelerated() {
//    return cv::ocl::useOpenCL();
//}
//
//
//void Viz2DWorker::setAccelerated(bool a) {
//#ifndef __EMSCRIPTEN__
//    if(a != cv::ocl::useOpenCL()) {
//        clglContext_->getCLExecContext().setUseOpenCL(a);
//        clvaContext_->getCLExecContext().setUseOpenCL(a);
//        cv::ocl::setUseOpenCL(a);
//        double w = 0;
//        double h = 0;
//        double fps = 0;
//        double fourcc = 0;
//
//        if(writer_) {
//            w = writer_->get(cv::CAP_PROP_FRAME_WIDTH);
//            h = writer_->get(cv::CAP_PROP_FRAME_HEIGHT);
//            fps = writer_->get(cv::CAP_PROP_FPS);
//            fourcc = writer_->get(cv::CAP_PROP_FOURCC);
//        }
//
//        if(a) {
//            if(capture_) {
//                delete capture_;
//                makeVACapture(capturePath_, vaCaptureDeviceIndex_);
//            }
//
//            if(writer_) {
//                delete writer_;
//                makeVAWriter(writerPath_, fourcc, fps, cv::Size(w, h), vaWriterDeviceIndex_);
//            }
//        } else {
//            if(capture_) {
//                delete capture_;
//                makeCapture(capturePath_);
//            }
//            if(writer_) {
//                delete writer_;
//                makeWriter(writerPath_, fourcc, fps, cv::Size(w, h));
//            }
//        }
//    }
//#endif
//}

bool Viz2DWorker::display() {
    bool result = true;
    if (!offscreen_) {
        glfwPollEvents();
        screen().draw_contents();
        clglContext_->blitFrameBufferToScreen(getViewport(), getWindowSize(), isStretching());
        makeCurrent();
        screen().draw_widgets();
        glfwSwapBuffers(glfwWindow_);
        result = !glfwWindowShouldClose(glfwWindow_);
    }

    return result;
}

bool Viz2DWorker::isClosed() {
    return closed_;

}
void Viz2DWorker::close() {
    setVisible(false);
    closed_ = true;
}

GLFWwindow* Viz2DWorker::getGLFWWindow() {
    return glfwWindow_;
}

NVGcontext* Viz2DWorker::getNVGcontext() {
    return screen().nvg_context();
}
}
}
