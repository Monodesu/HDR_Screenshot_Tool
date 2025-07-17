#pragma once
#include "WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

	// ���ص�ǰ�û��� Startup �ļ�������·����%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup��
	std::wstring GetStartupFolder();

	// �������ļ��д���/ɾ��ָ�� exePath �Ŀ�ݷ�ʽ (.lnk)
	bool CreateStartupShortcut(const std::wstring& exePath, const std::wstring& linkName = L"HDR Screenshot Tool.lnk");
	bool RemoveStartupShortcut(const std::wstring& linkName = L"HDR Screenshot Tool.lnk");

	// ��ѯ�ÿ�ݷ�ʽ�Ƿ����
	bool IsStartupShortcutPresent(const std::wstring& linkName = L"HDR Screenshot Tool.lnk");

} // namespace screenshot_tool