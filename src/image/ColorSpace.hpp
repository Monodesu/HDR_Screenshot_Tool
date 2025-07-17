#pragma once
#include <cstdint>

namespace screenshot_tool {

	class ColorSpace {
	public:
		// Rec.2020 to sRGB color space conversion
		static void Rec2020ToSRGB(float& r, float& g, float& b);
	};

	// 从 Rec.2020 PQ (HDR10) 格式转换为线性伽马（0-1）空间。
	// 接口：实现后再调用。
	void PQ2020ToLinearSRGB(const uint16_t* srcRGBA16F, int width, int height, int strideBytes, float maxLumNit, float minLumNit, float* outLinearRGB);

} // namespace screenshot_tool