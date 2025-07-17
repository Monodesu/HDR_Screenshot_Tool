#pragma once
#include <cstdint>
#include <string>

namespace screenshot_tool {

	class ImageSaverPNG {
	public:
		// ���� RGB8 ����Ϊ PNG��ʹ�� GDI+��
		bool SaveRGBToPNG(const std::wstring& path, const uint8_t* rgb, int w, int h);
	};

} // namespace screenshot_tool