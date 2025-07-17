#pragma once
#define WM_TRAYICON (WM_APP + 100)

#include "../platform/WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

    // 菜单项 ID
    enum TrayMenuId : UINT {
        IDM_TRAY_CAPTURE_REGION = 1000,
        IDM_TRAY_CAPTURE_FULLSCREEN,
        IDM_TRAY_OPEN_FOLDER,
        IDM_TRAY_TOGGLE_AUTOSTART,
        IDM_TRAY_TOGGLE_SAVEFILE,
        IDM_TRAY_TOGGLE_FULLSCREEN_CURRENT_MONITOR,
        IDM_TRAY_TOGGLE_REGION_FULLSCREEN_MONITOR,
        IDM_TRAY_EXIT
    };

    class TrayIcon {
    public:
        bool Create(HWND hwnd, UINT callbackMsg, HICON icon = nullptr, const wchar_t* tip = L"HDR Screenshot Tool");
        void Destroy();
        HMENU BuildContextMenu(bool autoStart, bool saveToFile);

    private:
        NOTIFYICONDATA nid_{};
        bool added_ = false;
    };

} // namespace screenshot_tool