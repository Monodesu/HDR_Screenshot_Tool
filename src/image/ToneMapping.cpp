#include "ToneMapping.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace screenshot_tool {
    
    void ToneMap_ACES(const float* inRGB, float* outRGB, int count, float) {
        const float a = 2.51f;
        const float b = 0.03f;
        const float c = 2.43f;
        const float d = 0.59f;
        const float e = 0.14f;
        
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < 3; j++) {
                float x = inRGB[i * 3 + j];
                outRGB[i * 3 + j] = std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
            }
        }
    }
    
    void ToneMap_Reinhard(const float* inRGB, float* outRGB, int count, float) {
        for (int i = 0; i < count * 3; i++) {
            outRGB[i] = inRGB[i] / (1.0f + inRGB[i]);
        }
    }
    
    float LinearToSRGB(float linear) {
        if (linear <= 0.0031308f) {
            return 12.92f * linear;
        } else {
            return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        }
    }
    
    float PQToLinear(float pq) {
        constexpr float m1 = 2610.0f / 16384.0f;
        constexpr float m2 = 2523.0f / 4096.0f * 128.0f;
        constexpr float c1 = 3424.0f / 4096.0f;
        constexpr float c2 = 2413.0f / 4096.0f * 32.0f;
        constexpr float c3 = 2392.0f / 4096.0f * 32.0f;

        pq = std::clamp(pq, 0.0f, 1.0f);
        if (pq == 0.0f) return 0.0f;

        float p = std::pow(pq, 1.0f / m2);
        float num = std::max(p - c1, 0.0f);
        float den = c2 - c3 * p;

        if (den <= 0.0f) return 0.0f;

        return std::pow(num / den, 1.0f / m1) * 10000.0f;
    }
    
    float HalfToFloat(uint16_t h) {
        uint16_t h_exp = (h & 0x7C00) >> 10;
        uint16_t h_sig = h & 0x03FF;
        uint32_t f_sgn = (h & 0x8000) << 16;
        uint32_t f_exp, f_sig;

        if (h_exp == 0) {
            if (h_sig == 0) {
                f_exp = 0;
                f_sig = 0;
            } else {
                // Subnormal half-precision number
                h_exp += 1;
                while ((h_sig & 0x0400) == 0) {
                    h_sig <<= 1;
                    h_exp -= 1;
                }
                h_sig &= 0x03FF;
                f_exp = (h_exp + (127 - 15)) << 23;
                f_sig = h_sig << 13;
            }
        } else if (h_exp == 0x1F) {
            f_exp = 0xFF << 23;
            f_sig = h_sig << 13;
        } else {
            f_exp = (h_exp + (127 - 15)) << 23;
            f_sig = h_sig << 13;
        }

        uint32_t f = f_sgn | f_exp | f_sig;
        float result;
        memcpy(&result, &f, sizeof(result));
        return result;
    }
    
} // namespace screenshot_tool