#pragma once
#include "ImageBuffer.hpp"
#include "../config/Config.hpp"
#include <dxgi.h>

namespace screenshot_tool {

	class PixelConvert {
	public:
		// 将各种支持格式转换为 8bit RGB，输出到 data vector
		static bool ConvertToRGB8(const ImageBuffer& in, ImageBuffer& outRGB8);
		
		// HDR 到 SDR 转换
		static bool ToSRGB8(DXGI_FORMAT fmt, ImageBuffer& buffer, bool isHDR = false, const Config* config = nullptr);
		
	private:
		// Format conversion helpers
		static void convertBGRA8ToRGB8(const ImageBuffer& in, ImageBuffer& out);
		static void convertRGBA16FToRGB8(const ImageBuffer& in, ImageBuffer& out);
		static void convertRGBA10A2ToRGB8(const ImageBuffer& in, ImageBuffer& out);
		
		// HDR/SDR processing functions
		static void processHDR16Float(ImageBuffer& buffer, const Config* config);
		static void processSDR16Float(ImageBuffer& buffer);
		static void processHDR10(ImageBuffer& buffer, const Config* config);
		static void processSDR10(ImageBuffer& buffer);
		static void processSDR(ImageBuffer& buffer);
	};

} // namespace screenshot_tool