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
        int  Run();       // ---- ��Ϣѭ�� -------------------------------------------------------------
        void Shutdown();  // ---- Ӧ���������� ---------------------------------------------------------

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);

        void onTrayMenu(UINT cmd);
        void doCaptureRegion();
        void doCaptureFullscreen();
        void onRegionSelected(const RECT& r);
        void applyAutoStart();
        void CaptureRect(const RECT& r);
        bool ensureCaptureReady(); // 确保捕获系统就绪，检测显示配置变化

        HINSTANCE      hInst_ = nullptr;
        HWND           hwnd_ = nullptr;
        TrayIcon       tray_;
        HotkeyManager  hotkeys_;
        SelectionOverlay overlay_;
        GDIPlusInit    gdipInit_;
        Config         cfg_;
        SmartCapture   capture_;
        bool           running_ = false;
        
        // 显示配置监控
        UINT           lastDisplayWidth_ = 0;
        UINT           lastDisplayHeight_ = 0;
        DWORD          lastDisplayChangeTime_ = 0;
    };

} // namespace screenshot_tool