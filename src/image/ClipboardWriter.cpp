#include "ClipboardWriter.hpp"
#include "../platform/WinHeaders.hpp"
#include "../util/ScopedWin.hpp"
#include <vector>
#include <memory>

namespace screenshot_tool {
	bool ClipboardWriter::WriteRGB(HWND hwnd, const uint8_t* rgb, int w, int h) {
		if (!OpenClipboard(hwnd)) return false;

		auto clipboardGuard = [](void*) { CloseClipboard(); };
		std::unique_ptr<void, decltype(clipboardGuard)> guard(reinterpret_cast<void*>(1), clipboardGuard);

		EmptyClipboard();

		// 计算位图数据大小
		int rowSize = ((w * 24 + 31) / 32) * 4; // 4字节对齐
		int imageSize = rowSize * h;
		int totalSize = sizeof(BITMAPINFOHEADER) + imageSize;

		auto hDib = GlobalAlloc(GMEM_MOVEABLE, totalSize);
		if (!hDib) return false;

		auto* bih = static_cast<BITMAPINFOHEADER*>(GlobalLock(hDib));
		if (!bih) {
			GlobalFree(hDib);
			return false;
		}

		// 填充位图信息头
		*bih = BITMAPINFOHEADER{
			.biSize = sizeof(BITMAPINFOHEADER),
			.biWidth = w,
			.biHeight = h, // 正数表示从下到上
			.biPlanes = 1,
			.biBitCount = 24,
			.biCompression = BI_RGB,
			.biSizeImage = static_cast<DWORD>(imageSize)
		};

		// 复制图像数据（需要垂直翻转，RGB转BGR）
		auto* dibData = reinterpret_cast<uint8_t*>(bih + 1);
		for (int y = 0; y < h; ++y) {
			auto* dstRow = dibData + (h - 1 - y) * rowSize;
			auto* srcRow = rgb + y * w * 3;
			for (int x = 0; x < w; ++x) {
				dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
				dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
				dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
			}
		}

		GlobalUnlock(hDib);

		if (SetClipboardData(CF_DIB, hDib)) {
			return true; // 成功时不释放内存，系统会管理
		}
		else {
			GlobalFree(hDib);
			return false;
		}
	}
} // namespace screenshot_tool