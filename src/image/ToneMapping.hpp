#pragma once
namespace screenshot_tool {

	// ���� ACES Filmic Tone Mapping / Reinhard ռλ���������� HDR����� 0-1 SDR
	void ToneMap_ACES(const float* inRGB, float* outRGB, int pixelCount, float targetNits = 250.0f);
	void ToneMap_Reinhard(const float* inRGB, float* outRGB, int pixelCount, float targetNits = 250.0f);

} // namespace screenshot_tool