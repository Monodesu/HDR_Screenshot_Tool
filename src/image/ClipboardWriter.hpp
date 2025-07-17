#pragma once
#include "../platform/WinHeaders.hpp"
#include <cstdint>
#include <string>

namespace screenshot_tool {
	class ClipboardWriter {
	public:
		// д�� RGB8 ���ص������� (CF_BITMAP)
		static bool WriteRGB(HWND hwnd, const uint8_t* rgb, int w, int h);
	};
} // namespace screenshot_tool