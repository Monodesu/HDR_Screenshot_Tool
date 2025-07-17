#pragma once
#include "CaptureCommon.hpp"
#include "../image/ImageBuffer.hpp"
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool {

    class GDICapture {
    public:
        bool CaptureRegion(int x, int y, int w, int h, ImageBuffer& outRGB8);
    };

} // namespace screenshot_tool