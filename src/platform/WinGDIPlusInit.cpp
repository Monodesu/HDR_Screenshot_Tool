#include "WinGDIPlusInit.hpp"
#include <gdiplus.h>

namespace screenshot_tool {
    std::atomic<int> GDIPlusInit::s_refCount{ 0 };
    ULONG_PTR        GDIPlusInit::s_gdiplusToken = 0;

    GDIPlusInit::GDIPlusInit() {
        if (s_refCount.fetch_add(1) == 0) {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&s_gdiplusToken, &input, nullptr);
        }
    }
    GDIPlusInit::~GDIPlusInit() {
        if (s_refCount.fetch_sub(1) == 1) {
            Gdiplus::GdiplusShutdown(s_gdiplusToken);
        }
    }
} // namespace screenshot_tool