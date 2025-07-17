#pragma once
#include "../platform/WinHeaders.hpp"
#include "../config/Config.hpp"
#include "../capture/SmartCapture.hpp"
#include "../ui/TrayIcon.hpp"
#include "../ui/HotkeyManager.hpp"
#include "../ui/SelectionOverlay.hpp"
#include "../platform/WinGDIPlusInit.hpp"
#include "../platform/WinShell.hpp"
#include "../platform/WinNotification.hpp"
#include "../util/Logger.hpp"

namespace screenshot_tool {

    class ScreenshotApp {
    public:
        ScreenshotApp();
        ~ScreenshotApp();

        bool Initialize(HINSTANCE hInst);
        int  Run();       // 消息循环
        void Shutdown();  // 清理

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);

        void onTrayMenu(UINT cmd);
        void doCaptureRegion();
        void doCaptureFullscreen();
        void onRegionSelected(const RECT& r);
        void applyAutoStart();
        void CaptureRect(const RECT& r);

        HINSTANCE      hInst_ = nullptr;
        HWND           hwnd_ = nullptr;
        TrayIcon       tray_;
        HotkeyManager  hotkeys_;
        SelectionOverlay overlay_;
        GDIPlusInit    gdipInit_;
        Config         cfg_;
        SmartCapture   capture_;
        bool           running_ = false;
    };

} // namespace screenshot_tool