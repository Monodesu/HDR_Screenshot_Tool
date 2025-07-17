#include "HotkeyManager.hpp"
#include "../util/HotkeyParse.hpp"

namespace screenshot_tool {

    bool HotkeyManager::RegisterHotkey(HWND hwnd, int id, const std::string& hotkeyText) {
        UINT mods = 0, vk = 0; if (!HotkeyParse::ParseHotkey(hotkeyText, mods, vk)) return false;
        return RegisterHotKey(hwnd, id, mods, vk) != FALSE;
    }

    void HotkeyManager::UnregisterHotkey(HWND hwnd, int id) {
        UnregisterHotKey(hwnd, id);
    }

} // namespace screenshot_tool