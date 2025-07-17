#pragma once
#include "WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

	// 返回当前用户的 Startup 文件夹完整路径（%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup）
	std::wstring GetStartupFolder();

	// 在启动文件夹创建/删除指向 exePath 的快捷方式 (.lnk)
	bool CreateStartupShortcut(const std::wstring& exePath, const std::wstring& linkName = L"HDR Screenshot Tool.lnk");
	bool RemoveStartupShortcut(const std::wstring& linkName = L"HDR Screenshot Tool.lnk");

	// 查询该快捷方式是否存在
	bool IsStartupShortcutPresent(const std::wstring& linkName = L"HDR Screenshot Tool.lnk");

} // namespace screenshot_tool