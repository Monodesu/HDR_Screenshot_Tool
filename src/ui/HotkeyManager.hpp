#pragma once
#include "../platform/WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

    class HotkeyManager {
    public:
        bool RegisterHotkey(HWND hwnd, int id, const std::string& hotkeyText); // สนำร util/HotkeyParse
        void UnregisterHotkey(HWND hwnd, int id);
    };

} // namespace screenshot_tool