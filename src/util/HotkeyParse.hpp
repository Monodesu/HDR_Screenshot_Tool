#pragma once
#include <string>
#include <cstdint>
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool {

	class HotkeyParse {
	public:
		// 解析热键字符串 "ctrl+shift+alt+a" 为 modifiers + VK_A
		// 返回 true 成功
		static bool ParseHotkey(const std::string& text, UINT& outModifiers, UINT& outVk);
	};

} // namespace screenshot_tool