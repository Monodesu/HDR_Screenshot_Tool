#include "ColorSpace.hpp"
#include <algorithm>

namespace screenshot_tool {
    
    void ColorSpace::Rec2020ToSRGB(float& r, float& g, float& b) {
        // 使用Bradford色度适应的精确转换矩阵
        float r2 = 1.7166511f * r - 0.3556708f * g - 0.2533663f * b;
        float g2 = -0.6666844f * r + 1.6164812f * g + 0.0157685f * b;
        float b2 = 0.0176399f * r - 0.0427706f * g + 0.9421031f * b;

        r = std::clamp(r2, 0.0f, 1.0f);
        g = std::clamp(g2, 0.0f, 1.0f);
        b = std::clamp(b2, 0.0f, 1.0f);
    }
    
    void PQ2020ToLinearSRGB(const uint16_t* input, int width, int height, int stride, 
                           float maxNits, float targetNits, float* output) {
        // TODO: 实现 HDR10 PQ -> Linear -> sRGB
        // This is a placeholder for more complex PQ to linear conversion
        // For now, delegate to the per-pixel functions in PixelConvert
    }
    
} // namespace screenshot_tool