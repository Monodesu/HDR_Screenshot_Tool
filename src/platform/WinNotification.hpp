#pragma once
#include "WinHeaders.hpp"
#include <string>

namespace screenshot_tool {

	class WinNotification {
	public:
	// 添加一个简易通知（气泡提示）封装；要求调用方已经在托盘注册 NOTIFYICONDATA。
	// 这里只封装 Shell_NotifyIcon 修改数据结构的文本部分。
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