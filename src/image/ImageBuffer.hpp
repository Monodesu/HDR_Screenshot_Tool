#pragma once
#include <cstdint>
#include <vector>

namespace screenshot_tool {

    enum class PixelFormat {
        Unknown,
        RGBA_F16,    // R16G16B16A16_FLOAT
        RGBA10A2,    // R10G10B10A2_UNORM
        BGRA8,       // BGRA8_UNORM
        RGB8         // planar / packed output (no alpha)
    };

    struct ImageBuffer {
        PixelFormat format = PixelFormat::Unknown;
        int width = 0;
        int height = 0;
        int stride = 0;              // bytes per row
        std::vector<uint8_t> data;   // raw pixels
    };

} // namespace screenshot_tool