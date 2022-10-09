#define CL_TARGET_OPENCL_VERSION 220

constexpr long unsigned int WIDTH = 1920;
constexpr long unsigned int HEIGHT = 1080;
constexpr double FPS = 30;
constexpr double OFFSCREEN = false;
constexpr const char* OUTPUT_FILENAME = "tetra-demo.mkv";

#include "subsystems.hpp"

using std::cerr;
using std::endl;

cv::ocl::OpenCLExecutionContext VA_CONTEXT;
cv::ocl::OpenCLExecutionContext GL_CONTEXT;

void render(cv::UMat& frameBuffer) {
    glBindFramebuffer(GL_FRAMEBUFFER, kb::gl::frame_buf);
    glViewport(0, 0, WIDTH , HEIGHT );
    glRotatef(1, 0, 1, 0);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glColor3f(1.0, 1.0, 1.0);
    glBegin(GL_LINES);
    for (GLfloat i = -2.5; i <= 2.5; i += 0.25) {
        glVertex3f(i, 0, 2.5);
        glVertex3f(i, 0, -2.5);
        glVertex3f(2.5, 0, i);
        glVertex3f(-2.5, 0, i);
    }
    glEnd();

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
    kb::gl::swapBuffers();
}

void glow(cv::UMat &src, int ksize = WIDTH / 85 % 2 == 0 ? WIDTH / 85  + 1 : WIDTH / 85) {
    static cv::UMat blur;
    static cv::UMat src16;

    cv::bitwise_not(src, src);

    //Resize for some extra performance (especially when blurring)
    cv::resize(src, blur, cv::Size(), 0.5, 0.5);
    //Cheap blur
    cv::boxFilter(blur, blur, -1, cv::Size(ksize, ksize), cv::Point(-1,-1), true, cv::BORDER_REPLICATE);
    //Back to original size
    cv::resize(blur, blur, cv::Size(WIDTH, HEIGHT));

    //Multiply the src image with a blurred version of itself
    cv::multiply(src, blur, src16, 1, CV_16U);
    //Normalize and convert back to CV_8U
    cv::divide(src16, cv::Scalar::all(255.0), src, 1, CV_8U);

    cv::bitwise_not(src, src);
}

int main(int argc, char **argv) {
    using namespace kb;
    //Initialize OpenCL Context for VAAPI
    va::init_va();
    /*
     * The OpenCLExecutionContext for VAAPI needs to be copied right after init_va().
     * Now everytime you want to do VAAPI interop first bind the context.
     */
    VA_CONTEXT = cv::ocl::OpenCLExecutionContext::getCurrent();

    //Initialize VP9 HW encoding using VAAPI
    cv::VideoWriter video(OUTPUT_FILENAME, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('V', 'P', '9', '0'), FPS, cv::Size(WIDTH, HEIGHT), { cv::VIDEOWRITER_PROP_HW_DEVICE, 0, cv::VIDEOWRITER_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_VAAPI, cv::VIDEOWRITER_PROP_HW_ACCELERATION_USE_OPENCL, 1 });

    //If we are rendering offscreen we don't need x11
    if(!OFFSCREEN)
        x11::init_x11();

    //Passing true to init_egl will create a OpenGL debug context
    egl::init_egl();
    //Initialize OpenCL Context for OpenGL
    gl::init_gl();
    /*
     * The OpenCLExecutionContext for OpenGL needs to be copied right after init_gl().
     * Now everytime you want to do OpenGL interop first bind the context.
     */
    GL_CONTEXT = cv::ocl::OpenCLExecutionContext::getCurrent();

    cerr << "VA Version: " << va::get_info() << endl;
    cerr << "EGL Version: " << egl::get_info() << endl;
    cerr << "OpenGL Version: " << gl::get_info() << endl;
    cerr << "OpenCL Platforms: " << endl << cl::get_info() << endl;

    cv::UMat frameBuffer(HEIGHT, WIDTH, CV_8UC4, cv::Scalar::all(0));
    cv::UMat videoFrame;

    uint64_t cnt = 1;
    int64 start = cv::getTickCount();
    double tickFreq = cv::getTickFrequency();

    while (true) {
        //Activate the OpenCL context for OpenGL
        GL_CONTEXT.bind();
        //Using OpenGL, render a rotating tetrahedron
        render(frameBuffer);
        //Transfer buffer ownership to OpenCL
        gl::fetch_frame_buffer(frameBuffer);
        //Using OpenCV/OpenCL for a glow effect
        glow(frameBuffer);
        //Color-conversion from BGRA to RGB. OpenCV/OpenCL.
        cv::cvtColor(frameBuffer, videoFrame, cv::COLOR_BGRA2RGB);
        //Video frame is upside down -> flip it
        cv::flip(videoFrame, videoFrame, 0);
        //Activate the OpenCL context for VAAPI
        VA_CONTEXT.bind();
        //Encode the frame using VAAPI on the GPU.
        video.write(videoFrame);

        if(x11::is_initialized()) {
            //Yet again activate the OpenCL context for OpenGL
            GL_CONTEXT.bind();
            //Transfer buffer ownership back to OpenGL
            gl::return_frame_buffer(frameBuffer);
            //Blit the framebuffer we have been working on to the screen
            gl::blit_frame_buffer_to_screen();
            //Check if the x11 window was closed
            if(x11::window_closed())
                break;
        }

        //Measure FPS
        if (cnt % uint64(FPS) == 0) {
            int64 tick = cv::getTickCount();
            cerr << "FPS : " << tickFreq / ((tick - start + 1) / cnt) << '\r';
            start = tick;
            cnt = 1;
        }

        ++cnt;
    }

    return 0;
}
