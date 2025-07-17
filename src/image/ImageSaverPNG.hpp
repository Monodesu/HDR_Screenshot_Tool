#pragma once
#include <cstdint>
#include <string>

namespace screenshot_tool {

	class ImageSaverPNG {
	public:
		// 保存 RGB8 数据为 PNG（使用 GDI+）
		bool SaveRGBToPNG(const std::wstring& path, const uint8_t* rgb, int w, int h);
	};

} // namespace screenshot_tool