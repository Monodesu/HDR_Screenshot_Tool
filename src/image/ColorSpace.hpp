#pragma once
#include <cstdint>

namespace screenshot_tool {

	// 将 Rec.2020 PQ (HDR10) 像素转换为线性浮点（0-1）空间。
	// 仅接口；实现后续补充。
	void PQ2020ToLinearSRGB(const uint16_t* srcRGBA16F, int width, int height, int strideBytes, float maxLumNit, float minLumNit, float* outLinearRGB);

} // namespace screenshot_tool