#pragma once
#include "WinHeaders.hpp"
#include <atomic>

namespace screenshot_tool {

    // RAII ≥ı ºªØ GDI+
    class GDIPlusInit {
    public:
        GDIPlusInit();
        ~GDIPlusInit();
    private:
        static std::atomic<int> s_refCount;
        static ULONG_PTR         s_gdiplusToken;
    };

} // namespace screenshot_tool