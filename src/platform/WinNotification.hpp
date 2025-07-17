#pragma once
#include "WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

	class WinNotification {
	public:
	// ���һ������֪ͨ��������ʾ����װ��Ҫ����÷��Ѿ�������ע�� NOTIFYICONDATA��
	// ����ֻ��װ Shell_NotifyIcon �޸����ݽṹ���ı����֡�
		static void ShowBalloon(HWND hwnd, 
			UINT iconId, 
			const std::wstring& title, 
			const std::wstring& msg, 
			DWORD icon = NIIF_INFO, 
			UINT timeoutMs = 3000);

		static void ShowBalloon(HWND hwnd,
			const std::wstring& title,
			const std::wstring& msg,
			DWORD icon = NIIF_INFO,
			UINT  timeoutMs = 3000);
	};

} // namespace screenshot_tool