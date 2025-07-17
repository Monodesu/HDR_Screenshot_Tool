#include "HotkeyParse.hpp"
#include "../platform/WinHeaders.hpp"
#include <algorithm>

namespace screenshot_tool {

    bool ParseHotkey(const std::string& text, UINT& outModifiers, UINT& outVk) {
        outModifiers = 0; outVk = 0;
        std::string s = text; std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        auto next = [&](const char* token, UINT mask) { if (s.find(token) != std::string::npos) outModifiers |= mask; };
        next("ctrl", MOD_CONTROL);
        next("shift", MOD_SHIFT);
        next("alt", MOD_ALT);
        // Win 键不常用；如需支持 next("win", MOD_WIN);
        char key = 0;
        for (char c : s) {
            if (c >= 'a' && c <= 'z') { key = c; }
            else if (c >= '0' && c <= '9') { key = c; }
        }
        if (!key) return false;
        outVk = (key >= '0' && key <= '9') ? (UINT)(key) : (UINT)(::VkKeyScanA(key) & 0xFF);
        return true;
    }

} // namespace screenshot_tool