#include "ClipboardWriter.hpp"
#include "../platform/WinHeaders.hpp"
#include "../util/ScopedWin.hpp"
#include <vector>

namespace screenshot_tool {
	bool WriteRGB(HWND, const uint8_t* rgb, int w, int h) {
		// TODO: ��ʵʵ�֣���ǰֱ�ӷ��� false �Ա���Էֲ�
		(void)rgb; (void)w; (void)h;
		return false;
	}
} // namespace screenshot_tool