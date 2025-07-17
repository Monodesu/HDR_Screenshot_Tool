#pragma once
#include <string>
#include <cstdint>

namespace screenshot_tool {

	// 解析类似 "ctrl+shift+alt+a" → modifiers + VK_A
	// 返回 true 成功
	bool ParseHotkey(const std::string& text, UINT& outModifiers, UINT& outVk);

} // namespace screenshot_tool