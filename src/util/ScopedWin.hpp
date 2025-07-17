#pragma once
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool {

    struct ScopedDC {
        HDC dc = nullptr; HWND wnd = nullptr; bool release = false;
        ScopedDC(HWND h) :dc(GetDC(h)), wnd(h), release(true) {}
        ~ScopedDC() { if (release && dc)ReleaseDC(wnd, dc); }
    };

    struct ScopedMemDC {
        HDC dc = nullptr; HBITMAP oldBmp = nullptr; ScopedMemDC(HDC src, int w, int h) {
            dc = CreateCompatibleDC(src);
            HBITMAP bmp = CreateCompatibleBitmap(src, w, h);
            oldBmp = (HBITMAP)SelectObject(dc, bmp);
        }
        ~ScopedMemDC() { if (dc) { HBITMAP bmp = (HBITMAP)SelectObject(dc, oldBmp); DeleteObject(bmp); DeleteDC(dc); } }
    };

    struct ScopedHGlobalLock {
        HGLOBAL h = nullptr; void* ptr = nullptr; ScopedHGlobalLock(HGLOBAL h_) :h(h_) { ptr = GlobalLock(h); } ~ScopedHGlobalLock() { if (ptr)GlobalUnlock(h); }
    };

} // namespace screenshot_tool