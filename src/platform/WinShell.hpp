#pragma once
#include "WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

	class WinShell {
	public:
		// ���ص�ǰ�û��� Startup �ļ�������·����%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup��
		static std::wstring GetStartupFolder();

		// �������ļ��д���/ɾ��ָ�� exePath �Ŀ�ݷ�ʽ (.lnk)
		static bool CreateStartupShortcut(const std::wstring& exePath, const std::wstring& linkName = L"HDR Screenshot Tool.lnk");
		static bool RemoveStartupShortcut(const std::wstring& linkName = L"HDR Screenshot Tool.lnk");

		// ��ѯ�ÿ�ݷ�ʽ�Ƿ����
		static bool IsStartupShortcutPresent(const std::wstring& linkName = L"HDR Screenshot Tool.lnk");
	};

} // namespace screenshot_tool