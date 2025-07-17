#include "ImageSaverPNG.hpp"
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool {

    bool SaveRGBToPNG(const std::wstring& path, const uint8_t* rgb, int w, int h) {
        // TODO:  π”√ GDI+ Bitmap + CLSID_PNG ±£¥Ê
        (void)path; (void)rgb; (void)w; (void)h;
        return false;
    }

} // namespace screenshot_tool