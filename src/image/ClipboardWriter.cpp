#include "ClipboardWriter.hpp"
#include "../platform/WinHeaders.hpp"
#include "../util/ScopedWin.hpp"
#include <vector>

namespace screenshot_tool {
	bool ClipboardWriter::WriteRGB(HWND hwnd, const uint8_t* rgb, int w, int h) {
		// TODO: 待实现，目前直接返回 false 以便编译通过
		(void)hwnd; (void)rgb; (void)w; (void)h;
		return false;
	}
} // namespace screenshot_tool