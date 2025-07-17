#pragma once
#include <cstdint>
#include <string>
#include <Windows.h>

namespace screenshot_tool {

	class ImageSaverPNG {
	public:
		// 保存 RGB8 数据为 PNG，使用 GDI+
		static bool SaveRGBToPNG(const uint8_t* rgb, int w, int h, const wchar_t* savePath);
		
	private:
		static bool GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
	};

} // namespace screenshot_tool