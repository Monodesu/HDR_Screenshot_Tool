#include "GDICapture.hpp"
#include "../util/ScopedWin.hpp"

namespace screenshot_tool {

    bool GDICapture::CaptureRegion(int x, int y, int w, int h, ImageBuffer& out) {
        HDC scr = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(scr);
        HBITMAP bmp = CreateCompatibleBitmap(scr, w, h);
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, w, h, scr, x, y, SRCCOPY | CAPTUREBLT);

        BITMAPINFO bmi{}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
        std::vector<uint8_t> bgra(w * h * 4);
        GetDIBits(mem, bmp, 0, h, bgra.data(), &bmi, DIB_RGB_COLORS);

        out.format = PixelFormat::RGB8; out.width = w; out.height = h; out.stride = w * 3; out.data.resize(out.stride * h);
        for (int i = 0; i < w * h; i++) {
            out.data[i * 3 + 0] = bgra[i * 4 + 2];
            out.data[i * 3 + 1] = bgra[i * 4 + 1];
            out.data[i * 3 + 2] = bgra[i * 4 + 0];
        }

        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, scr);
        return true;
    }

} // namespace screenshot_tool