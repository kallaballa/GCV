#define CL_TARGET_OPENCL_VERSION 120

#include "../common/viz2d.hpp"
#include "../common/nvg.hpp"
#include "../common/util.hpp"

#include <string>
#include <algorithm>
#include <vector>
#include <sstream>
#include <limits>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/** Application parameters **/

constexpr unsigned int WIDTH = 1920;
constexpr unsigned int HEIGHT = 1080;
const unsigned long DIAG = hypot(double(WIDTH), double(HEIGHT));
constexpr bool OFFSCREEN = false;
constexpr const char* OUTPUT_FILENAME = "font-demo.mkv";
constexpr const int VA_HW_DEVICE_INDEX = 0;
constexpr double FPS = 60;

/** Visualization parameters **/

constexpr float FONT_SIZE = 40.0f;
constexpr float MAX_STAR_SIZE = 1.0f;
constexpr int MIN_STAR_COUNT = 1000;
constexpr int MAX_STAR_COUNT = 3000;
constexpr float MIN_STAR_LIGHTNESS = 1.0f;
constexpr int MIN_STAR_ALPHA = 5;
// Intensity of bloom effect defined by kernel size. The default scales with the image diagonal.
const int bloom_kernel_size = std::max(int(DIAG / 200 % 2 == 0 ? DIAG / 200  + 1 : DIAG / 200), 1);

using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::istringstream;

cv::Ptr<kb::viz2d::Viz2D> v2d = new kb::viz2d::Viz2D(cv::Size(WIDTH, HEIGHT), cv::Size(WIDTH, HEIGHT), OFFSCREEN, "Font Demo");
vector<string> lines;
//Derive the transformation matrix tm for the pseudo 3D effect from quad1 and quad2.
vector<cv::Point2f> quad1 = {{0,0},{WIDTH,0},{WIDTH,HEIGHT},{0,HEIGHT}};
vector<cv::Point2f> quad2 = {{WIDTH/3,0},{WIDTH/1.5,0},{WIDTH,HEIGHT},{0,HEIGHT}};
cv::Mat tm = cv::getPerspectiveTransform(quad1, quad2);
//BGRA
cv::UMat stars, warped;

void iteration() {
    static size_t cnt = 0;
    int y = 0;

    v2d->nanovg([&](const cv::Size& sz) {
        using namespace kb::viz2d::nvg;
        v2d->clear();

        fontSize(FONT_SIZE);
        fontFace("sans-bold");
        fillColor(kb::viz2d::color_convert(cv::Scalar(0.15 * 180.0, 128, 255, 255), cv::COLOR_HLS2BGR));
        textAlign(NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

        /** only draw lines that are visible **/

        //Total number of lines in the text
        off_t numLines = lines.size();
        //Height of the text in pixels
        off_t textHeight = (numLines * FONT_SIZE);
        //How many pixels to translate the text up.
        off_t translateY = HEIGHT - cnt;
        translate(0, translateY);

        for (const auto &line : lines) {
            if (translateY + y > -textHeight && translateY + y <= HEIGHT) {
                text(WIDTH / 2.0, y, line.c_str(), line.c_str() + line.size());
                y += FONT_SIZE;
            } else {
                //We can stop reading lines if the current line exceeds the page.
                break;
            }
        }
    });

    if(y == 0) {
        //Nothing drawn, exit.
        exit(0);
    }

    v2d->opencl([&](cv::UMat& frameBuffer){
        //Pseudo 3D text effect.
        cv::warpPerspective(frameBuffer, warped, tm, frameBuffer.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar());
        //Combine layers
        cv::add(stars, warped, frameBuffer);
    });

    update_fps(v2d, true);

#ifndef __EMSCRIPTEN__
    v2d->write();
#endif

    //If onscreen rendering is enabled it displays the framebuffer in the native window. Returns false if the window was closed.
    if(!v2d->display())
        exit(0);

    ++cnt;
    //Wrap the cnt around if it becomes to big.
    if(cnt > std::numeric_limits<size_t>().max() / 2.0)
        cnt = 0;
}

int main(int argc, char **argv) {
    try {
    using namespace kb::viz2d;

    print_system_info();
    if(!v2d->isOffscreen()) {
        v2d->setVisible(true);
    }
#ifndef __EMSCRIPTEN__
    v2d->makeVAWriter(OUTPUT_FILENAME, cv::VideoWriter::fourcc('V', 'P', '9', '0'), FPS, v2d->getFrameBufferSize(), VA_HW_DEVICE_INDEX);
#endif

    //The text to display
    string txt = cv::getBuildInformation();
    //Save the text to a vector
    std::istringstream iss(txt);

    for (std::string line; std::getline(iss, line); ) {
        lines.push_back(line);
    }

    cv::RNG rng(cv::getTickCount());

    v2d->nanovg([&](const cv::Size& sz) {
        using namespace kb::viz2d::nvg;
        v2d->clear();
        //draw stars
        int numStars = rng.uniform(MIN_STAR_COUNT, MAX_STAR_COUNT);
        for(int i = 0; i < numStars; ++i) {
            beginPath();
            strokeWidth(rng.uniform(0.5f, MAX_STAR_SIZE));
            strokeColor(color_convert(cv::Scalar(0, rng.uniform(MIN_STAR_LIGHTNESS, 1.0f) * 255, 255, rng.uniform(MIN_STAR_ALPHA, 255)), cv::COLOR_HLS2BGR));
            circle(rng.uniform(0, WIDTH) , rng.uniform(0, HEIGHT), MAX_STAR_SIZE);
            stroke();
        }
    });

    v2d->opencl([&](cv::UMat& frameBuffer){
        frameBuffer.copyTo(stars);
    });

    //Frame count.
#ifndef __EMSCRIPTEN__
    while(true)
        iteration();
#else
    emscripten_set_main_loop(iteration, -1, false);
#endif

    } catch(std::exception& ex) {
        cerr << "Exception: " << ex.what() << endl;
    }
    return 0;
}
