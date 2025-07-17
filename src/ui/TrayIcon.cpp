#include "TrayIcon.hpp"

namespace screenshot_tool {

    bool TrayIcon::Create(HWND hwnd, UINT id, HICON icon, const wchar_t* tip) {
        nid_.cbSize = sizeof(nid_);
        nid_.hWnd = hwnd;
        nid_.uID = id;
        nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid_.uCallbackMessage = WM_TRAYICON;
        nid_.hIcon = icon ? icon : LoadIcon(nullptr, IDI_APPLICATION);
        wcsncpy_s(nid_.szTip, tip, _TRUNCATE);
        added_ = Shell_NotifyIcon(NIM_ADD, &nid_) != FALSE;
        return added_;
    }

    void TrayIcon::Destroy() {
        if (added_) {
            Shell_NotifyIcon(NIM_DELETE, &nid_);
            added_ = false;
        }
    }

    void TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& msg, DWORD icon, UINT timeoutMs) {
        if (!added_) return;
        nid_.uFlags = NIF_INFO;
        wcsncpy_s(nid_.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(nid_.szInfo, msg.c_str(), _TRUNCATE);
        nid_.dwInfoFlags = icon; nid_.uTimeout = timeoutMs;
        Shell_NotifyIcon(NIM_MODIFY, &nid_);
    }

    HMENU TrayIcon::BuildContextMenu(bool autoStart, bool saveToFile) {
        HMENU menu = CreatePopupMenu();
        AppendMenu(menu, MF_STRING, IDM_TRAY_CAPTURE_REGION, L"�����ͼ(&R)");
        AppendMenu(menu, MF_STRING, IDM_TRAY_CAPTURE_FULLSCREEN, L"ȫ����ͼ(&F)");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_STRING | (autoStart ? MF_CHECKED : 0), IDM_TRAY_TOGGLE_AUTOSTART, L"��������(&S)");
        AppendMenu(menu, MF_STRING | (saveToFile ? MF_CHECKED : 0), IDM_TRAY_TOGGLE_SAVEFILE, L"���浽�ļ�(&V)");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_STRING, IDM_TRAY_EXIT, L"�˳�(&X)");
        return menu;
    }

} // namespace screenshot_tool