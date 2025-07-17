#include "SmartCapture.hpp"
#include <thread>

namespace screenshot_tool {

    // ---- ��ʼ�� ---------------------------------------------------------------
    bool SmartCapture::Initialize()
    {
        Logger::Debug(L"SmartCapture::Initialize()");
        // DXGI ��ʼ��ʧ�ܲ�������������������ʱ���Զ� fallback
        dxgi_.Initialize();
        return true;
    }

    // ---- ����ڣ�CaptureToFileAndClipboard -----------------------------------
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

        // д������
        if (!ClipboardWriter::WriteRGB(hwnd, rgb8.data.data(), w, h)) {
            Logger::Warn(L"ClipboardWriter failed");
        }

        // PNG ���棨��ѡ·��Ϊ���򲻱��棩
        if (savePath && *savePath) {
            if (!ImageSaverPNG::SaveRGBToPNG(rgb8.data.data(), w, h, savePath)) {
                Logger::Warn(L"ImageSaverPNG failed");
            }
        }

        return usedGDI ? Result::FallbackGDI : Result::OK;
    }

    // ---- ȫ����ͼ (���������ǰ������) --------------------------------------
    SmartCapture::Result SmartCapture::CaptureFullscreen(HWND hwnd, const RECT& virtualRect,
        const wchar_t* savePath)
    {
        return CaptureToFileAndClipboard(hwnd, virtualRect, savePath);
    }

    // ---- �ڲ������� DXGI �� ʧ�� fallback GDI ----------------------------------
    bool SmartCapture::captureRegionInternal(int x, int y, int w, int h,
        ImageBuffer& outRGB8,
        bool& usedGDI)
    {
        usedGDI = false;

        // �ȳ��� DXGI
        if (dxgi_.IsInitialized()) {
            DXGI_FORMAT fmt{};
            if (dxgi_.CaptureRegion(x, y, w, h, fmt, outRGB8) == CaptureResult::Success) {
                PixelConvert::ToSRGB8(fmt, outRGB8); // HDR �� SDR
                return true;
            }
            else {
                Logger::Warn(L"DXGI Capture failed, fallback to GDI");
                dxgi_.Reinitialize(); // ������������һ�ο��ָܻ�
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
