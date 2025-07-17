#pragma once
#include <cstdint>

namespace screenshot_tool {

	// �� Rec.2020 PQ (HDR10) ����ת��Ϊ���Ը��㣨0-1���ռ䡣
	// ���ӿڣ�ʵ�ֺ������䡣
	void PQ2020ToLinearSRGB(const uint16_t* srcRGBA16F, int width, int height, int strideBytes, float maxLumNit, float minLumNit, float* outLinearRGB);

} // namespace screenshot_tool