#define CL_TARGET_OPENCL_VERSION 120

constexpr long unsigned int WIDTH = 1920;
constexpr long unsigned int HEIGHT = 1080;
constexpr double FPS = 60;
constexpr bool OFFSCREEN = false;
constexpr const char* OUTPUT_FILENAME = "tetra-demo.mkv";
constexpr const int VA_HW_DEVICE_INDEX = 0;

#include "../common/subsystems.hpp"

using std::cerr;
using std::endl;

void init_render() {
    glViewport(0, 0, WIDTH, HEIGHT);
    glColor3f(1.0, 1.0, 1.0);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-2, 2, -1.5, 1.5, 1, 40);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -3);
    glRotatef(50, 1, 0, 0);
    glRotatef(70, 0, 1, 0);
}

void render() {
    //Render a tetrahedron using immediate mode because the code is more concise for a demo
    glBindFramebuffer(GL_FRAMEBUFFER, kb::gl::frame_buf);
    glViewport(0, 0, WIDTH , HEIGHT );
    glRotatef(1, 0, 1, 0);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBegin(GL_TRIANGLE_STRIP);
        glColor3f(1, 1, 1);
        glVertex3f(0, 2, 0);
        glColor3f(1, 0, 0);
        glVertex3f(-1, 0, 1);
        glColor3f(0, 1, 0);
        glVertex3f(1, 0, 1);
        glColor3f(0, 0, 1);
        glVertex3f(0, 0, -1.4);
        glColor3f(1, 1, 1);
        glVertex3f(0, 2, 0);
        glColor3f(1, 0, 0);
        glVertex3f(-1, 0, 1);
    glEnd();
    glFlush();
}

void glow_effect(cv::UMat &src, int ksize = WIDTH / 85 % 2 == 0 ? WIDTH / 85  + 1 : WIDTH / 85) {
    static cv::UMat resize;
    static cv::UMat blur;
    static cv::UMat src16;

    cv::bitwise_not(src, src);

    //Resize for some extra performance
    cv::resize(src, resize, cv::Size(), 0.5, 0.5);
    //Cheap blur
    cv::boxFilter(resize, resize, -1, cv::Size(ksize, ksize), cv::Point(-1,-1), true, cv::BORDER_REPLICATE);
    //Back to original size
    cv::resize(resize, blur, cv::Size(WIDTH, HEIGHT));

    //Multiply the src image with a blurred version of itself
    cv::multiply(src, blur, src16, 1, CV_16U);
    //Normalize and convert back to CV_8U
    cv::divide(src16, cv::Scalar::all(255.0), src, 1, CV_8U);

    cv::bitwise_not(src, src);
}

int main(int argc, char **argv) {
    using namespace kb;
    //Initialize OpenCL Context for VAAPI
    va::init();

    //Initialize VP9 HW encoding using VAAPI
    cv::VideoWriter encoder(OUTPUT_FILENAME, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('V', 'P', '9', '0'), FPS, cv::Size(WIDTH, HEIGHT), {
            cv::VIDEOWRITER_PROP_HW_DEVICE, VA_HW_DEVICE_INDEX,
            cv::VIDEOWRITER_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI,
            cv::VIDEOWRITER_PROP_HW_ACCELERATION_USE_OPENCL, 1
    });

    //If we are rendering offscreen we don't need x11
    if(!OFFSCREEN)
        x11::init();

    //Passing true to init_egl will create a OpenGL debug context
    egl::init();
    //Initialize OpenCL Context for OpenGL
    gl::init();

    cerr << "VA Version: " << va::get_info() << endl;
    cerr << "EGL Version: " << egl::get_info() << endl;
    cerr << "OpenGL Version: " << gl::get_info() << endl;
    cerr << "OpenCL Platforms: " << endl << cl::get_info() << endl;

    cv::UMat frameBuffer;
    cv::UMat videoFrame;

    uint64_t cnt = 1;
    int64 start = cv::getTickCount();
    double tickFreq = cv::getTickFrequency();
    double lastFps = FPS;

    init_render();

    while (true) {
        //Activate the OpenCL context for OpenGL
        gl::bind();
        //Render using OpenGL
        render();

        //Aquire the frame buffer for use by OpenCL
        gl::acquire_from_gl(frameBuffer);
        //Glow effect (OpenCL)
        glow_effect(frameBuffer);
        //Color-conversion from BGRA to RGB. OpenCV/OpenCL.
        cv::cvtColor(frameBuffer, videoFrame, cv::COLOR_BGRA2RGB);
        //Release the frame buffer for use by OpenGL
        gl::release_to_gl(frameBuffer);

        if(x11::is_initialized()) {
            //Blit the framebuffer we have been working on to the screen
            gl::blit_frame_buffer_to_screen();

            //Check if the x11 window was closed
            if(x11::window_closed())
                break;

            //Transfer the back buffer (which we have been using as frame buffer) to the native window
            gl::swap_buffers();
        }

        //Activate the OpenCL context for VAAPI
        va::bind();
        //Video frame is upside down because the frameBuffer is. -> flip it (OpenCL)
        cv::flip(videoFrame, videoFrame, 0);
        //Encode the frame using VAAPI on the GPU.
        encoder.write(videoFrame);

        //Measure FPS
        if (cnt % uint64(ceil(lastFps)) == 0) {
            int64 tick = cv::getTickCount();
            lastFps = tickFreq / ((tick - start + 1) / cnt);
            cerr << "FPS : " << lastFps << '\r';
            start = tick;
            cnt = 1;
        }

        ++cnt;
    }

    return 0;
}
