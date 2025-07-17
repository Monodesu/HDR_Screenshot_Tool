#include "ClipboardWriter.hpp"
#include "../platform/WinHeaders.hpp"
#include "../util/ScopedWin.hpp"
#include <vector>

namespace screenshot_tool {
	bool WriteRGB(HWND, const uint8_t* rgb, int w, int h) {
		// TODO: 真实实现；当前直接返回 false 以便调试分层
		(void)rgb; (void)w; (void)h;
		return false;
	}
} // namespace screenshot_tool