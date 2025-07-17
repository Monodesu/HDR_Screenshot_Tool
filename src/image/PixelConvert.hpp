#pragma once
#include "ImageBuffer.hpp"
#include <dxgi.h>

namespace screenshot_tool {

	class PixelConvert {
	public:
		// 将各种支持格式转换为 8bit RGB，输出到 data vector
		static bool ConvertToRGB8(const ImageBuffer& in, ImageBuffer& outRGB8);
		
		// HDR 到 SDR 转换
		static bool ToSRGB8(DXGI_FORMAT fmt, ImageBuffer& buffer);
	};

} // namespace screenshot_tool