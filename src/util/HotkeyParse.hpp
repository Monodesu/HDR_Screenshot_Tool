#pragma once
#include <string>
#include <cstdint>

namespace screenshot_tool {

	// �������� "ctrl+shift+alt+a" �� modifiers + VK_A
	// ���� true �ɹ�
	bool ParseHotkey(const std::string& text, UINT& outModifiers, UINT& outVk);

} // namespace screenshot_tool