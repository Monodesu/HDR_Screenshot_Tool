#include "ImageSaverPNG.hpp"
#include "../platform/WinHeaders.hpp"
#include <mutex>

namespace screenshot_tool {

    bool ImageSaverPNG::SaveRGBToPNG(const uint8_t* rgb, int w, int h, const wchar_t* savePath) {
        using namespace Gdiplus;
        
        Bitmap bitmap(w, h, PixelFormat24bppRGB);
        BitmapData bitmapData;
        Rect rect(0, 0, w, h);

        if (bitmap.LockBits(&rect, ImageLockModeWrite, PixelFormat24bppRGB, &bitmapData) == Ok) {
            auto* dst = static_cast<uint8_t*>(bitmapData.Scan0);
            for (int y = 0; y < h; ++y) {
                auto* dstRow = dst + y * bitmapData.Stride;
                auto* srcRow = rgb + y * w * 3;
                for (int x = 0; x < w; ++x) {
                    dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
                    dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
                    dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
                }
            }
            bitmap.UnlockBits(&bitmapData);

            // 获取PNG编码器
            CLSID pngClsid;
            if (GetEncoderClsid(L"image/png", &pngClsid)) {
                return bitmap.Save(savePath, &pngClsid, nullptr) == Ok;
            }
        }
        return false;
    }
    
    bool ImageSaverPNG::GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
        using namespace Gdiplus;
        
        UINT num = 0, size = 0;
        GetImageEncodersSize(&num, &size);
        if (size == 0) return false;

        auto pImageCodecInfo = std::make_unique<uint8_t[]>(size);
        auto* codecInfo = reinterpret_cast<ImageCodecInfo*>(pImageCodecInfo.get());

        GetImageEncoders(num, size, codecInfo);

        for (UINT j = 0; j < num; ++j) {
            if (wcscmp(codecInfo[j].MimeType, format) == 0) {
                *pClsid = codecInfo[j].Clsid;
                return true;
            }
        }

        return false;
    }

} // namespace screenshot_tool