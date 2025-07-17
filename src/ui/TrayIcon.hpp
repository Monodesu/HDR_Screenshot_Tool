#pragma once
#define WM_TRAYICON (WM_APP + 100)

#include "../platform/WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

    // �˵��� ID
    enum TrayMenuId : UINT {
        IDM_TRAY_CAPTURE_REGION = 1000,
        IDM_TRAY_CAPTURE_FULLSCREEN,
        IDM_TRAY_OPEN_FOLDER,
        IDM_TRAY_TOGGLE_AUTOSTART,
        IDM_TRAY_TOGGLE_SAVEFILE,
        IDM_TRAY_EXIT
    };

    class TrayIcon {
    public:
        bool Create(HWND hwnd, UINT id = 1, HICON icon = nullptr, const wchar_t* tip = L"HDR Screenshot Tool");
        void Destroy();
        void ShowBalloon(const std::wstring& title, const std::wstring& msg, DWORD icon = NIIF_INFO, UINT timeoutMs = 3000);
        HMENU BuildContextMenu(bool autoStart, bool saveToFile);

    private:
        NOTIFYICONDATA nid_{};
        bool added_ = false;
    };

} // namespace screenshot_tool