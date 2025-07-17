#include "WinNotification.hpp"

namespace screenshot_tool {

    void ShowBalloonTip(HWND hwnd, UINT iconId, const std::wstring& title, const std::wstring& msg, DWORD icon, UINT timeoutMs) {
        NOTIFYICONDATA nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = iconId;
        nid.uFlags = NIF_INFO;
        wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(nid.szInfo, msg.c_str(), _TRUNCATE);
        nid.dwInfoFlags = icon;
        nid.uTimeout = timeoutMs; // Windows < Vista
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }

} // namespace screenshot_tool