#ifndef SRC_SUBSYSTEMS_HPP_
#define SRC_SUBSYSTEMS_HPP_

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <filesystem>
#include <thread>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#define NANOGUI_USE_OPENGL
#include <nanogui/nanogui.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <CL/cl.h>
#include <CL/cl_gl.h>
#include <opencv2/core/ocl.hpp>
#include <opencv2/core/opengl.hpp>
#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>
#include <nanogui/opengl.h>

using std::cout;
using std::cerr;
using std::endl;
using std::string;

namespace kb {

typedef cv::ocl::OpenCLExecutionContext CLExecContext_t;
typedef cv::ocl::OpenCLExecutionContextScope CLExecScope_t;

void gl_check_error(const std::filesystem::path &file, unsigned int line, const char *expression) {
    GLint errorCode = glGetError();

    if (errorCode != GL_NO_ERROR) {
        cerr << "GL failed in " << file.filename() << " (" << line << ") : " << "\nExpression:\n   " << expression << "\nError code:\n   " << errorCode << "\n   " << endl;
        assert(false);
    }
}
#define GL_CHECK(expr)                            \
    expr;                                        \
    kb::gl_check_error(__FILE__, __LINE__, #expr);

namespace app {
unsigned int window_width;
unsigned int window_height;
bool offscreen;
} //app

namespace display {
GLFWwindow* window;

GLFWwindow* get_window() {
    return window;
}

void terminate(GLFWwindow *win = display::get_window()) {
    glfwDestroyWindow(win);
    glfwTerminate();
}

std::pair<int,int> frameBufferSize(GLFWwindow *win = display::get_window()) {
    int fbW, fbH;
    glfwGetFramebufferSize(win, &fbW, &fbH);
    return {fbW, fbH};
}

std::pair<int,int> windowSize(GLFWwindow *win = display::get_window()) {
    int w, h;
    glfwGetWindowSize(win, &w, &h);
    return {w, h};
}

float get_pixel_ratio(GLFWwindow *win = display::get_window()) {
#if defined(_WIN32)
    HWND hWnd = glfwGetWin32Window(win);
    HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    /* The following function only exists on Windows 8.1+, but we don't want to make that a dependency */
    static HRESULT (WINAPI *GetDpiForMonitor_)(HMONITOR, UINT, UINT*, UINT*) = nullptr;
    static bool GetDpiForMonitor_tried = false;

    if (!GetDpiForMonitor_tried) {
        auto shcore = LoadLibrary(TEXT("shcore"));
        if (shcore)
            GetDpiForMonitor_ = (decltype(GetDpiForMonitor_)) GetProcAddress(shcore, "GetDpiForMonitor");
        GetDpiForMonitor_tried = true;
    }

    if (GetDpiForMonitor_) {
        uint32_t dpiX, dpiY;
        if (GetDpiForMonitor_(monitor, 0 /* effective DPI */, &dpiX, &dpiY) == S_OK)
            return std::round(dpiX / 96.0);
    }
    return 1.f;
#else
    return (float)frameBufferSize(win).first / (float)windowSize(win).first;
#endif
}
void update_size(GLFWwindow *win = display::get_window()) {
    float pixelRatio = display::get_pixel_ratio(win);
    glfwSetWindowSize(win, app::window_width * pixelRatio, app::window_height * pixelRatio);
    glViewport(0, 0, app::window_width * pixelRatio, app::window_height * pixelRatio);
}

bool is_fullscreen(GLFWwindow* win = display::get_window()) {
    return glfwGetWindowMonitor(win) != nullptr;
}

void set_fullscreen(bool f, GLFWwindow* win = display::get_window()) {
    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if(f) {
        glfwSetWindowMonitor(win, monitor, 0,0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(win, nullptr, 0,0,app::window_width, app::window_height,mode->refreshRate);
    }
    display::update_size();
}

void framebuffer_size_callback(GLFWwindow *win, int width, int height) {
    display::update_size();
}

void error_callback(int error, const char *description) {
    fprintf(stderr, "Error: %s\n", description);
}

void init(const string &title, int major, int minor, int samples, bool debug) {
    assert(glfwInit() == GLFW_TRUE);
    glfwSetErrorCallback(error_callback);

    if(debug)
        glfwWindowHint (GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    if(app::offscreen)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    glfwSetTime(0);

#ifdef __APPLE__
    glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
    glfwWindowHint(GLFW_SAMPLES, samples);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(app::window_width, app::window_height, title.c_str(), nullptr, nullptr);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(11);
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
}
} // namespace display

namespace gl {
//code in the kb::gl namespace deals with OpenGL (and OpenCV/GL) internals
cv::ogl::Texture2D *frame_buf_tex;
GLuint frame_buf;
GLuint render_buf;
CLExecContext_t context;

void begin() {
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, frame_buf));
    GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, render_buf));
    GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, render_buf));
    frame_buf_tex->bind();
}

void end() {
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0));
    GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));

    //glFlush seems enough but i wanna make sure that there won't be race conditions.
    //At least on TigerLake/Iris it doesn't make a difference in performance.
    GL_CHECK(glFlush());
    GL_CHECK(glFinish());
}

void render(std::function<void(int,int)> fn) {
    CLExecScope_t scope(gl::context);
    gl::begin();
    fn(app::window_width, app::window_height);
    gl::end();
}

void init() {
    glewExperimental = true;
    glewInit();

    cv::ogl::ocl::initializeContextFromGL();

    frame_buf = 0;
    GL_CHECK(glGenFramebuffers(1, &frame_buf));
    GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frame_buf));
    GL_CHECK(glGenRenderbuffers(1, &render_buf));
    GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, render_buf));
    GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, app::window_width, app::window_height));

    GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, render_buf));
    frame_buf_tex = new cv::ogl::Texture2D(cv::Size(app::window_width, app::window_height), cv::ogl::Texture2D::RGBA, false);
    frame_buf_tex->bind();

    GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame_buf_tex->texId(), 0));

    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    gl::context = CLExecContext_t::getCurrent();
}

std::string get_info() {
    return reinterpret_cast<const char*>(glGetString(GL_VERSION));
}

void blit_frame_buffer_to_screen() {
    GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, kb::gl::frame_buf));
    GL_CHECK(glReadBuffer(GL_COLOR_ATTACHMENT0));
    GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
    GL_CHECK(glBlitFramebuffer(0, 0, app::window_width, app::window_height, 0, 0, app::window_width, app::window_height, GL_COLOR_BUFFER_BIT, GL_NEAREST));
}
} // namespace gl

namespace cl {
cv::UMat frameBuffer;

void acquire_from_gl(cv::UMat& m) {
    gl::begin();
    GL_CHECK(cv::ogl::convertFromGLTexture2D(*gl::frame_buf_tex, m));
    //The OpenGL frameBuffer is upside-down. Flip it. (OpenCL)
    cv::flip(m, m, 0);
}

void release_to_gl(cv::UMat& m) {
    //The OpenGL frameBuffer is upside-down. Flip it back. (OpenCL)
    cv::flip(m, m, 0);
    GL_CHECK(cv::ogl::convertToGLTexture2D(m, *gl::frame_buf_tex));
    gl::end();
}

void compute(std::function<void(cv::UMat&)> fn) {
    CLExecScope_t scope(gl::context);
    acquire_from_gl(cl::frameBuffer);
    fn(cl::frameBuffer);
    release_to_gl(cl::frameBuffer);
}

std::string get_info() {
    std::stringstream ss;
    std::vector<cv::ocl::PlatformInfo> plt_info;
    cv::ocl::getPlatfomsInfo(plt_info);
    const cv::ocl::Device &defaultDevice = cv::ocl::Device::getDefault();
    cv::ocl::Device current;
    ss << endl;
    for (const auto &info : plt_info) {
        for (int i = 0; i < info.deviceNumber(); ++i) {
            ss << "\t";
            info.getDevice(current, i);
            if (defaultDevice.name() == current.name())
                ss << "* ";
            else
                ss << "  ";
            ss << info.version() << " = " << info.name() << endl;
            ss << "\t\t  GL sharing: " << (current.isExtensionSupported("cl_khr_gl_sharing") ? "true" : "false") << endl;
            ss << "\t\t  VAAPI media sharing: " << (current.isExtensionSupported("cl_intel_va_api_media_sharing") ? "true" : "false") << endl;
        }
    }

    return ss.str();
}
} //namespace cl

namespace va {
CLExecContext_t context;
cv::UMat videoFrame;

void copy() {
    va::context = CLExecContext_t::getCurrent();
}

bool read(std::function<void(cv::UMat&)> fn) {
    {
        CLExecScope_t scope(va::context);
        fn(va::videoFrame);
    }
    {
        CLExecScope_t scope(gl::context);
        cl::acquire_from_gl(cl::frameBuffer);
        if(va::videoFrame.empty())
            return false;
        //Color-conversion from RGB to BGRA (OpenCL)
        cv::cvtColor(va::videoFrame, cl::frameBuffer, cv::COLOR_RGB2BGRA);
        cv::resize(cl::frameBuffer, cl::frameBuffer, cv::Size(app::window_width, app::window_height));
        cl::release_to_gl(cl::frameBuffer);
    }
    return true;
}

void write(std::function<void(const cv::UMat&)> fn) {
    CLExecScope_t scope(va::context);
    //Color-conversion from BGRA to RGB. (OpenCL)
    cv::cvtColor(cl::frameBuffer, va::videoFrame, cv::COLOR_BGRA2RGB);
    cv::flip(va::videoFrame, va::videoFrame, 0);
    fn(va::videoFrame);
}
} // namespace va

namespace gui {
using namespace nanogui;
ref<nanogui::Screen> screen;
FormHelper* form;

nanogui::detail::FormWidget<bool> * make_gui_variable(const string& name, bool& v, const string& tooltip = "") {
    using kb::gui::form;
    auto var = form->add_variable(name, v);
    if(!tooltip.empty())
        var->set_tooltip(tooltip);
    return var;
}

template <typename T> nanogui::detail::FormWidget<T> * make_gui_variable(const string& name, T& v, const T& min, const T& max, bool spinnable = true, const string& unit = "", const string tooltip = "") {
    using kb::gui::form;
    auto var = form->add_variable(name, v);
    var->set_spinnable(spinnable);
    var->set_min_value(min);
    var->set_max_value(max);
    if(!unit.empty())
        var->set_units(unit);
    if(!tooltip.empty())
        var->set_tooltip(tooltip);
    return var;
}

void init(int w, int h, GLFWwindow* win = display::get_window()) {
    screen = new nanogui::Screen();
    screen->initialize(win, false);
    screen->set_size(nanogui::Vector2i(w, h));
    form = new FormHelper(screen);

    glfwSetCursorPosCallback(win,
            [](GLFWwindow *, double x, double y) {
        gui::screen->cursor_pos_callback_event(x, y);
        }
    );
    glfwSetMouseButtonCallback(win,
        [](GLFWwindow *, int button, int action, int modifiers) {
        gui::screen->mouse_button_callback_event(button, action, modifiers);
        }
    );
    glfwSetKeyCallback(win,
        [](GLFWwindow *, int key, int scancode, int action, int mods) {
        gui::screen->key_callback_event(key, scancode, action, mods);
        }
    );
    glfwSetCharCallback(win,
        [](GLFWwindow *, unsigned int codepoint) {
        gui::screen->char_callback_event(codepoint);
        }
    );
    glfwSetDropCallback(win,
        [](GLFWwindow *, int count, const char **filenames) {
        gui::screen->drop_callback_event(count, filenames);
        }
    );
    glfwSetScrollCallback(win,
        [](GLFWwindow *, double x, double y) {
        gui::screen->scroll_callback_event(x, y);
       }
    );
    glfwSetFramebufferSizeCallback(win,
        [](GLFWwindow *, int width, int height) {
            gui::screen->resize_callback_event(width, height);
        }
    );
}

void set_visible(bool v) {
    gui::screen->set_visible(v);
    gui::screen->perform_layout();
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
}

void update_size() {
    float pixelRatio = display::get_pixel_ratio();
    gui::screen->set_size(nanogui::Vector2i(app::window_width * pixelRatio, app::window_height * pixelRatio));
    display::update_size();
}
} //namespace gui

namespace nvg {
void clear(const float& r = 0.0f, const float& g = 0.0f, const float& b = 0.0f, const float& a = 1.0f) {
    GL_CHECK(glClearColor(r, g, b, a));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
}

void begin() {
    gl::begin();

    float r = display::get_pixel_ratio();
    float w = app::window_width;
    float h = app::window_height;
    NVGcontext* vg = gui::screen->nvg_context();
    GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, kb::gl::frame_buf));
    nvgSave(vg);
    GL_CHECK(glViewport(0, 0,w * r, h * r));
    nvgBeginFrame(vg, w, h, r);
}

void end() {
    NVGcontext* vg = gui::screen->nvg_context();
    nvgEndFrame(vg);
    nvgRestore(vg);
    gl::end();
}

void render(std::function<void(NVGcontext*,int,int)> fn) {
    CLExecScope_t scope(gl::context);
    nvg::begin();
    fn(gui::screen->nvg_context(), app::window_width, app::window_height);
    nvg::end();
}

void init() {
    nvgCreateFont(gui::screen->nvg_context(), "libertine", "assets/LinLibertine_RB.ttf");

    //workaround for first frame color glitch
    cv::UMat tmp;
    cl::acquire_from_gl(tmp);
    cl::release_to_gl(tmp);
}
} //namespace nvg

namespace app {
void print_system_info() {
    cerr << "OpenGL Version: " << gl::get_info() << endl;
    cerr << "OpenCL Platforms: " << cl::get_info() << endl;
}

void init(const string &windowTitle, unsigned int width, unsigned int height, bool offscreen = false, bool fullscreen = false, int major = 4, int minor = 6, int samples = 0, bool debugContext = false) {
    using namespace kb::gui;
    app::window_width = width;
    app::window_height = height;
    app::offscreen = offscreen;

    display::init(windowTitle, major, minor, samples, debugContext);
    gui::init(width, height);
    gl::init();
    nvg::init();
}

void run(std::function<void()> fn) {
    if(!app::offscreen)
        gui::set_visible(true);
    gui::update_size();

    fn();
}

bool display() {
    if(!app::offscreen) {
        glfwPollEvents();
        gui::screen->draw_contents();
        gl::blit_frame_buffer_to_screen();
        gui::screen->draw_widgets();
        auto* win = display::get_window();
        glfwSwapBuffers(win);
        return !glfwWindowShouldClose(win);
    }
    return true;
}

void update_fps(bool graphical = false) {
    static uint64_t cnt = 0;
    static double fps = 1;
    static cv::TickMeter meter;

    if (cnt > 0) {
        meter.stop();

        if (cnt % uint64(ceil(fps)) == 0) {
            fps = meter.getFPS();
            cerr << "FPS : " << fps << '\r';
            cnt = 0;
        }
    }

    if (graphical) {
        nvg::render([&](NVGcontext* vg, int w, int h) {
            string text = "FPS: " + std::to_string(fps);

            nvgBeginPath(vg);
            nvgRoundedRect(vg, 10, 10, 30 * text.size() + 10, 60, 10);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 180));
            nvgFill (vg);

            nvgBeginPath(vg);
            nvgFontSize(vg, 60.0f);
            nvgFontFace(vg, "mono");
            nvgFillColor(vg, nvgRGBA(90, 90, 90, 255));
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgText(vg, 22, 37, text.c_str(), nullptr);
        });
    }

    meter.start();
    ++cnt;
}

void terminate() {
    display::terminate();
    exit(0);
}
} //namespace app
} //namespace kb

#endif /* SRC_SUBSYSTEMS_HPP_ */
