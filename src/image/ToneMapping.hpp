#pragma once
namespace screenshot_tool {

	// 简易 ACES Filmic Tone Mapping / Reinhard 占位；输入线性 HDR，输出 0-1 SDR
	void ToneMap_ACES(const float* inRGB, float* outRGB, int pixelCount, float targetNits = 250.0f);
	void ToneMap_Reinhard(const float* inRGB, float* outRGB, int pixelCount, float targetNits = 250.0f);

} // namespace screenshot_tool