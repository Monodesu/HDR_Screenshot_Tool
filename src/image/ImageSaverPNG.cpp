#include "ImageSaverPNG.hpp"
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool {

    bool SaveRGBToPNG(const std::wstring& path, const uint8_t* rgb, int w, int h) {
        // TODO: ʹ�� GDI+ Bitmap + CLSID_PNG ����
        (void)path; (void)rgb; (void)w; (void)h;
        return false;
    }

} // namespace screenshot_tool