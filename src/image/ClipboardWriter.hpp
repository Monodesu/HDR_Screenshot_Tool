#pragma once
#include <cstdint>
#include <string>

namespace screenshot_tool {

	// Ğ´Èë RGB8 ÏñËØµ½¼ôÌù°å (CF_BITMAP)
	bool WriteRGBToClipboard(HWND hwnd, const uint8_t* rgb, int w, int h);

} // namespace screenshot_tool