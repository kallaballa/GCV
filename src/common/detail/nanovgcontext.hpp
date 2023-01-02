#ifndef SRC_COMMON_NANOVGCONTEXT_HPP_
#define SRC_COMMON_NANOVGCONTEXT_HPP_

#ifndef __EMSCRIPTEN__
#define NANOGUI_USE_OPENGL
#else
#define NANOGUI_USE_GLES
#define NANOGUI_GLES_VERSION 3
#endif
#include "clglcontext.hpp"
#include <nanogui/nanogui.h>
#include <nanogui/opengl.h>
#include "../util.hpp"
#include "../nvg.hpp"

namespace kb {
namespace viz2d {
namespace detail {
class NanoVGContext {
    Viz2DWorker& v2d_;
    NVGcontext *context_;
    CLGLContext &clglContext_;
public:
    class Scope {
        NanoVGContext& ctx_;
    public:
        Scope(NanoVGContext& ctx) : ctx_(ctx) {
            ctx_.begin();
        }

        ~Scope() {
            ctx_.end();
        }
    };
    NanoVGContext(Viz2DWorker& v2d, NVGcontext *context, CLGLContext &fbContext);
    void render(Storage& v2d, std::function<void(Storage&, const cv::Size&)> fn);
private:
    void begin();
    void end();
};
}
}
}

#endif /* SRC_COMMON_NANOVGCONTEXT_HPP_ */
