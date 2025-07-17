#include "SmartCapture.hpp"

namespace screenshot_tool {

    bool SmartCapture::Initialize() {
        dxgi_.Initialize(); // 失败也无所谓，后面 fallback GDI
        return true;
    }

    bool SmartCapture::captureRegionInternal(int x, int y, int w, int h, ImageBuffer& outRGB8) {
        if (dxgi_.IsInitialized()) {
            ImageBuffer raw{};
            auto res = dxgi_.CaptureRegion(x, y, w, h, raw);
            if (res == CaptureResult::Success) {
                return ConvertToRGB8(raw, outRGB8);
            }
            if (res == CaptureResult::NeedsReinitialize) dxgi_.Reinitialize();
        }
        // fallback GDI
        return gdi_.CaptureRegion(x, y, w, h, outRGB8);
    }

    bool SmartCapture::CaptureRegionAndOutput(HWND hwnd, const RECT& r, std::wstring* outSavedPath) {
        ImageBuffer rgb{};
        if (!captureRegionInternal(r.left, r.top, r.right - r.left, r.bottom - r.top, rgb)) return false;

        // 剪贴板
        WriteRGBToClipboard(hwnd, rgb.data.data(), rgb.width, rgb.height);

        // 保存文件
        if (cfg_ && cfg_->saveToFile) {
            std::wstring dir = ResolveSavePath(cfg_->savePath);
            if (cfg_->autoCreateSaveDir) EnsureDirectory(dir);
            std::wstring name = MakeTimestampedPngName();
            std::wstring full = dir + L"\\" + name;
            SaveRGBToPNG(full, rgb.data.data(), rgb.width, rgb.height);
            if (outSavedPath) *outSavedPath = full;
        }
        return true;
    }

    bool SmartCapture::CaptureFullscreenAndOutput(HWND hwnd, std::wstring* outSavedPath) {
        RECT vr = dxgi_.IsInitialized() ? dxgi_.GetVirtualRect() : RECT{ 0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN) };
        return CaptureRegionAndOutput(hwnd, vr, outSavedPath);
    }

} // namespace screenshot_tool