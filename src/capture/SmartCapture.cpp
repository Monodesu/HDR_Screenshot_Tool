#include "SmartCapture.hpp"
#include <thread>

namespace screenshot_tool {

    // ---- 初始化 ---------------------------------------------------------------
    bool SmartCapture::Initialize()
    {
        Logger::Debug(L"SmartCapture::Initialize()");
        // DXGI 初始化失败不至于致命；后续捕获时会自动 fallback
        dxgi_.Initialize();
        return true;
    }

    // ---- 主入口：CaptureToFileAndClipboard -----------------------------------
    SmartCapture::Result SmartCapture::CaptureToFileAndClipboard(HWND hwnd, const RECT& r,
        const wchar_t* savePath)
    {
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        if (w <= 0 || h <= 0) return Result::Failed;

        ImageBuffer rgb8;
        bool usedGDI = false;
        if (!captureRegionInternal(r.left, r.top, w, h, rgb8, usedGDI)) {
            Logger::Error(L"SmartCapture: captureRegionInternal failed");
            return Result::Failed;
        }

        // 写剪贴板
        if (!ClipboardWriter::WriteRGB(hwnd, rgb8.data(), w, h)) {
            Logger::Warn(L"ClipboardWriter failed");
        }

        // PNG 保存（可选路径为空则不保存）
        if (savePath && *savePath) {
            if (!ImageSaverPNG::SaveRGBtoPNG(rgb8.data(), w, h, savePath)) {
                Logger::Warn(L"ImageSaverPNG failed");
            }
        }

        return usedGDI ? Result::FallbackGDI : Result::OK;
    }

    // ---- 全屏截图 (虚拟桌面或当前监视器) --------------------------------------
    SmartCapture::Result SmartCapture::CaptureFullscreen(HWND hwnd, const RECT& virtualRect,
        const wchar_t* savePath)
    {
        return CaptureToFileAndClipboard(virtualRect, savePath);
    }

    // ---- 内部：尝试 DXGI → 失败 fallback GDI ----------------------------------
    bool SmartCapture::captureRegionInternal(int x, int y, int w, int h,
        ImageBuffer& outRGB8,
        bool& usedGDI)
    {
        usedGDI = false;

        // 先尝试 DXGI
        if (dxgi_.IsInitialized()) {
            DXGI_FORMAT fmt{};
            if (dxgi_.CaptureRegion(x, y, w, h, fmt, outRGB8)) {
                PixelConvert::ToSRGB8(fmt, outRGB8); // HDR → SDR
                return true;
            }
            else {
                Logger::Warn(L"DXGI Capture failed, fallback to GDI");
                dxgi_.Reinitialize(); // 尝试重连，下一次可能恢复
            }
        }

        // GDI fallback
        usedGDI = true;
        if (gdi_.CaptureRegion(x, y, w, h, outRGB8)) {
            return true;
        }

        return false;
    }

} // namespace screenshot_tool
