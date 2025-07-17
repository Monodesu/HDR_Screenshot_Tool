#include "ToneMapping.hpp"
#include <algorithm>
namespace screenshot_tool {
    void ToneMap_ACES(const float* inRGB, float* outRGB, int count, float) {
        // 极简占位：直接 clamp
        for (int i = 0; i < count * 3; i++) outRGB[i] = std::clamp(inRGB[i], 0.0f, 1.0f);
    }
    void ToneMap_Reinhard(const float* inRGB, float* outRGB, int count, float) {
        for (int i = 0; i < count * 3; i++) outRGB[i] = inRGB[i] / (1.0f + inRGB[i]);
    }
} // namespace screenshot_tool