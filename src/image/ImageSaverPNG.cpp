#include "ImageSaverPNG.hpp"
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool {

    bool ImageSaverPNG::SaveRGBToPNG(const uint8_t* rgb, int w, int h, const wchar_t* savePath) {
        // TODO: 使用 GDI+ Bitmap + CLSID_PNG 编码器
        (void)rgb; (void)w; (void)h; (void)savePath;
        return false;
    }

} // namespace screenshot_tool