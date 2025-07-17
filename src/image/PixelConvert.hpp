#pragma once
#include "ImageBuffer.hpp"

namespace screenshot_tool {

	// 将任意支持格式转换为 8bit RGB（输出 data vector）
	bool ConvertToRGB8(const ImageBuffer& in, ImageBuffer& outRGB8);

} // namespace screenshot_tool