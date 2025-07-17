#pragma once
#include "WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

	// ���һ������֪ͨ��������ʾ����װ��Ҫ����÷��Ѿ�������ע�� NOTIFYICONDATA��
	// ����ֻ��װ Shell_NotifyIcon �޸����ݽṹ���ı����֡�
	void ShowBalloonTip(HWND hwnd, UINT iconId, const std::wstring& title, const std::wstring& msg, DWORD icon = NIIF_INFO, UINT timeoutMs = 3000);

} // namespace screenshot_tool