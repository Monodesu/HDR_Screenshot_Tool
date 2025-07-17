#include "PixelConvert.hpp"
#include <algorithm>

namespace screenshot_tool {

    bool ConvertToRGB8(const ImageBuffer& in, ImageBuffer& out) {
        if (in.format == PixelFormat::RGB8) { out = in; return true; }
        out.format = PixelFormat::RGB8; out.width = in.width; out.height = in.height; out.stride = in.width * 3; out.data.resize(out.stride * out.height);
        // TODO: 实现格式转换 (BGRA8, RGBA_F16, RGBA10A2)
        std::fill(out.data.begin(), out.data.end(), 0);
        return true;
    }

} // namespace screenshot_tool