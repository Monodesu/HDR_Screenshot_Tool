#include "SmartCapture.hpp"
#include <thread>

namespace screenshot_tool {

    // ---- 初始化 ---------------------------------------------------------------
    bool SmartCapture::Initialize()
    {
        Logger::Debug(L"SmartCapture::Initialize()");
        // DXGI 初始化失败后自动 fallback
        dxgi_.Initialize();
        return true;
    }

    // ---- 捕获窗口：CaptureToFileAndClipboard -----------------------------------
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

        // 写入剪贴板
        if (!ClipboardWriter::WriteRGB(hwnd, rgb8.data.data(), w, h)) {
            Logger::Warn(L"ClipboardWriter failed");
        }

        // PNG 保存（路径为空则不保存）
        if (savePath && *savePath) {
            if (!ImageSaverPNG::SaveRGBToPNG(rgb8.data.data(), w, h, savePath)) {
                Logger::Warn(L"ImageSaverPNG failed");
            }
        }

        return usedGDI ? Result::FallbackGDI : Result::OK;
    }

    // ---- 全屏截图 (捕获整个虚拟桌面) --------------------------------------
    SmartCapture::Result SmartCapture::CaptureFullscreen(HWND hwnd, const RECT& virtualRect,
        const wchar_t* savePath)
    {
        return CaptureToFileAndClipboard(hwnd, virtualRect, savePath);
    }

    // ---- 内部捕获逻辑：DXGI 或 失败时回退到 GDI ----------------------------------
    bool SmartCapture::captureRegionInternal(int x, int y, int w, int h,
        ImageBuffer& outRGB8,
        bool& usedGDI)
    {
        usedGDI = false;

        // 优先尝试 DXGI
        if (dxgi_.IsInitialized()) {
            DXGI_FORMAT fmt{};
            CaptureResult result = dxgi_.CaptureRegion(x, y, w, h, fmt, outRGB8);
            
            if (result == CaptureResult::Success) {
                // HDR 转 SDR，传递 HDR 状态和配置
                bool isHDR = dxgi_.IsHDREnabled();
                PixelConvert::ToSRGB8(fmt, outRGB8, isHDR, cfg_);
                return true;
            }
            else if (result == CaptureResult::NeedsReinitialize) {
                Logger::Warn(L"DXGI needs reinitialization, fallback to GDI");
                dxgi_.Reinitialize(); // 尝试重新初始化，下一次可能恢复
            }
            else {
                Logger::Warn(L"DXGI Capture failed, fallback to GDI");
            }
        }

        // GDI fallback
        usedGDI = true;
        if (gdi_.CaptureRegion(x, y, w, h, outRGB8)) {
            return true;
        }

        return false;
    }

    RECT SmartCapture::GetVirtualDesktop() const {
        return RECT{ 
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)
        };
    }

} // namespace screenshot_tool
