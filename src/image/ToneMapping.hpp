#pragma once
#include <cstdint>

namespace screenshot_tool {

	// 实现 ACES Filmic Tone Mapping / Reinhard 占位，将输入的 HDR数值-> 0-1 SDR
	void ToneMap_ACES(const float* inRGB, float* outRGB, int pixelCount, float targetNits = 250.0f);
	void ToneMap_Reinhard(const float* inRGB, float* outRGB, int pixelCount, float targetNits = 250.0f);
	
	// Helper functions for HDR processing
	float LinearToSRGB(float linear);
	float PQToLinear(float pq);
	float HalfToFloat(uint16_t h);

} // namespace screenshot_tool