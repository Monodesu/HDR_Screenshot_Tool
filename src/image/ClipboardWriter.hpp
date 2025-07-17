#pragma once
#include <cstdint>
#include <string>

namespace screenshot_tool {

	// д�� RGB8 ���ص������� (CF_BITMAP)
	bool WriteRGBToClipboard(HWND hwnd, const uint8_t* rgb, int w, int h);

} // namespace screenshot_tool