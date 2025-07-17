#include "../platform/WinHeaders.hpp"
#include "ScreenshotApp.hpp"

#include "../config/Config.hpp"
#include "../util/Logger.hpp"
#include "../util/PathUtils.hpp"
#include "../util/StringUtils.hpp"
#include "../util/TimeUtils.hpp"
#include "../ui/TrayIcon.hpp"
#include "../ui/HotkeyManager.hpp"
#include "../ui/SelectionOverlay.hpp"
#include "../platform/WinShell.hpp"
#include "../platform/WinNotification.hpp"
#include "../platform/WinGDIPlusInit.hpp"
#include "../capture/SmartCapture.hpp"

#include <string>
#include <string_view>
#include <format>
#include <cassert>

// ============================================================================
// 修复注释
// ---- 初始化窗口 & 资源 ----------------------------------------------------
// ---- 托盘菜单处理 ---------------------------------------------------------
// ---- 区域截图 -------------------------------------------------------------
// ---- 全屏截图 -------------------------------------------------------------
// ---- Overlay 区域选择 -> App 接收 WM_ST_REGION_DONE -----------------------
// ---- 实现执行截图逻辑，支持全屏 -------------------------------------------
// ---- 应用自动启动设置，创建/删除快捷方式 ----------------------------------
// ---- Window Proc 静态 -> 实例 ---------------------------------------------
// ---- 实例消息处理 ---------------------------------------------------------
// ============================================================================
namespace screenshot_tool {

	// 自定义消息 ID
	static constexpr UINT WM_ST_TRAYICON = WM_APP + 1;
	static constexpr UINT WM_ST_REGION_DONE = WM_APP + 2;

	// 热键 ID（与 HotkeyManager 内部映射一致）
	static constexpr int HOTKEY_ID_REGION = 1;
	static constexpr int HOTKEY_ID_FULLSCREEN = 2;

	// ----------------------------------------------------------------------------
	// 确保屏幕截图保存目录存在，若不存在则自动创建
	// ----------------------------------------------------------------------------
	static std::wstring ensureSaveDir(const Config& cfg) {
		std::wstring dirW = StringUtils::Utf8ToWide(cfg.savePath);
		if (dirW.empty()) {
			// 默认：当前目录下的 Screenshots
			dirW = L"Screenshots";
		}
		if (!PathUtils::IsAbsolute(dirW)) {
			std::wstring exeDir = PathUtils::GetModuleDirectoryW();
			PathUtils::JoinInplace(exeDir, dirW); // exeDir += L"\\" + dirW
			dirW = exeDir;
		}
		PathUtils::CreateDirectoriesRecursive(dirW);
		return dirW;
	}

	// ----------------------------------------------------------------------------
	// ScreenshotApp 实现
	// ----------------------------------------------------------------------------
	ScreenshotApp::ScreenshotApp() : capture_(&cfg_) {

	}

	ScreenshotApp::~ScreenshotApp() {
		Shutdown();
	}

	bool ScreenshotApp::Initialize(HINSTANCE hInst) {
		hInst_ = hInst;

		// 1) 加载配置
		LoadConfig(cfg_, L"config.ini"); // Config.cpp 提供实现
		Logger::Info(L"Config loaded.");

		// 2) 确保截图保存目录存在
		ensureSaveDir(cfg_);

		// 3) 注册窗口 & 创建主窗口
		const wchar_t* kClassName = L"HDRScreenshotAppWnd";
		WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ScreenshotApp::WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = sizeof(LONG_PTR);
		wc.hInstance = hInst_;
		wc.hIcon = LoadIcon(hInst_, MAKEINTRESOURCE(1)); // 资源中的1号图标占位
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszMenuName = nullptr;
		wc.lpszClassName = kClassName;
		wc.hIconSm = LoadIcon(hInst_, MAKEINTRESOURCE(1));
		if (!RegisterClassEx(&wc)) {
			Logger::Error(L"RegisterClassEx failed");
			return false;
		}

		hwnd_ = CreateWindowEx(
			WS_EX_TOOLWINDOW,
			kClassName,
			L"HDR Screenshot Tool",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			nullptr, nullptr, hInst_, this);
		if (!hwnd_) {
			Logger::Error(L"CreateWindowEx failed");
			return false;
		}

		// 4) 初始化托盘图标
		if (!tray_.Create(hwnd_, WM_ST_TRAYICON, nullptr, L"HDR Screenshot Tool")) {
			Logger::Error(L"Tray icon init failed");
		}

		// 5) 热键注册
		if (!hotkeys_.RegisterHotkey(hwnd_, HOTKEY_ID_REGION, cfg_.regionHotkey)) {
			Logger::Warn(L"Register region hotkey failed: {}", StringUtils::Utf8ToWide(cfg_.regionHotkey));
		}
		if (!hotkeys_.RegisterHotkey(hwnd_, HOTKEY_ID_FULLSCREEN, cfg_.fullscreenHotkey)) {
			Logger::Warn(L"Register fullscreen hotkey failed: {}", StringUtils::Utf8ToWide(cfg_.fullscreenHotkey));
		}

		// 6) Overlay初始化（区域选择）
		auto regionCallback = [this](const RECT& rect) {
			onRegionSelected(rect);
		};
		if (!overlay_.Create(hInst_, hwnd_, regionCallback)) {
			Logger::Warn(L"Overlay create failed (region capture disabled)");
		}

		// 7) Capture 初始化（底层 DXGI + GDI 捕获）
		if (!capture_.Initialize()) {
			Logger::Warn(L"Capture init failed; will rely on GDI fallback");
		}

		// 8) 自动启动设置为 true
		applyAutoStart();

		running_ = true;
		return true;
	}

	int ScreenshotApp::Run() {
		MSG msg;
		while (running_ && GetMessage(&msg, nullptr, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		return (int)msg.wParam;
	}

	void ScreenshotApp::Shutdown() {
		if (!running_) return;
		running_ = false;

		hotkeys_.UnregisterHotkey(hwnd_, HOTKEY_ID_REGION);
		hotkeys_.UnregisterHotkey(hwnd_, HOTKEY_ID_FULLSCREEN);
		tray_.Destroy();

		if (hwnd_) {
			DestroyWindow(hwnd_);
			hwnd_ = nullptr;
		}

		SaveConfig(cfg_, L"config.ini");
		Logger::Info(L"App shutdown.");
	}

	// ----------------------------------------------------------------------------
	// 托盘菜单处理
	// ----------------------------------------------------------------------------
	void ScreenshotApp::onTrayMenu(UINT cmd) {
		switch (cmd) {
		case TrayMenuId::IDM_TRAY_CAPTURE_REGION:     doCaptureRegion();      break;
		case TrayMenuId::IDM_TRAY_CAPTURE_FULLSCREEN: doCaptureFullscreen();  break;
		case TrayMenuId::IDM_TRAY_OPEN_FOLDER: {
			auto dirW = ensureSaveDir(cfg_);
			ShellExecute(nullptr, L"open", dirW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			break;
		}
		case TrayMenuId::IDM_TRAY_TOGGLE_AUTOSTART:
			cfg_.autoStart = !cfg_.autoStart;
			applyAutoStart();
			break;
		case TrayMenuId::IDM_TRAY_EXIT:
			PostMessage(hwnd_, WM_CLOSE, 0, 0);
			break;
		default: break;
		}
	}

	// ----------------------------------------------------------------------------
	// 区域截图
	// ----------------------------------------------------------------------------
	void ScreenshotApp::doCaptureRegion() {
		if (!overlay_.IsValid()) {
			Logger::Warn(L"Region overlay invalid");
			return;
		}
		overlay_.BeginSelect();
	}

	// ----------------------------------------------------------------------------
	// 全屏截图
	// ----------------------------------------------------------------------------
	void ScreenshotApp::doCaptureFullscreen() {
		// TODO: 实现捕获当前显示器或整个屏幕
		RECT vr = capture_.GetVirtualDesktop();
		CaptureRect(vr); // 暂时使用相同接口，后续扩展
	}

	// ----------------------------------------------------------------------------
	// Overlay 区域选择 -> App 接收 WM_ST_REGION_DONE
	// ----------------------------------------------------------------------------
	void ScreenshotApp::onRegionSelected(const RECT& r) {
		CaptureRect(r);
	}

	// ----------------------------------------------------------------------------
	// 实现执行截图逻辑，支持全屏
	// ----------------------------------------------------------------------------
	void ScreenshotApp::CaptureRect(const RECT& r) {
		std::wstring savePath = ensureSaveDir(cfg_);
		std::wstring filename = PathUtils::MakeTimestampedPngNameW();
		
		// 构建完整的文件路径
		std::wstring fullPath = savePath;
		if (!fullPath.empty() && fullPath.back() != L'\\') {
			fullPath += L'\\';
		}
		fullPath += filename;

		SmartCapture::Result res = capture_.CaptureToFileAndClipboard(hwnd_, r, cfg_.saveToFile ? fullPath.c_str() : nullptr);
		switch (res) {
		case SmartCapture::Result::OK:
			if (cfg_.showNotification) {
				WinNotification::ShowBalloon(hwnd_, L"截图已保存", filename.c_str());
			}
			break;
		case SmartCapture::Result::FallbackGDI:
			if (cfg_.showNotification) {
				WinNotification::ShowBalloon(hwnd_, L"DXGI 失败，改用 GDI", filename.c_str());
			}
			break;
		default:
			if (cfg_.showNotification) {
				WinNotification::ShowBalloon(hwnd_, L"截图失败", L"请查看日志");
			}
			break;
		}
	}

	// ----------------------------------------------------------------------------
	// 应用自动启动设置，创建/删除快捷方式
	// ----------------------------------------------------------------------------
	void ScreenshotApp::applyAutoStart() {
		if (cfg_.autoStart) {
			// 获取当前执行文件路径
			wchar_t exePath[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePath, MAX_PATH);
			WinShell::CreateStartupShortcut(exePath);
		}
		else {
			WinShell::RemoveStartupShortcut();
		}
	}

	// ----------------------------------------------------------------------------
	// Window Proc 静态 -> 实例
	// ----------------------------------------------------------------------------
	LRESULT CALLBACK ScreenshotApp::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		ScreenshotApp* self = nullptr;
		if (msg == WM_NCCREATE) {
			auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
			self = reinterpret_cast<ScreenshotApp*>(cs->lpCreateParams);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
			self->hwnd_ = hWnd;
		}
		else {
			self = reinterpret_cast<ScreenshotApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		}
		if (self) {
			return self->instanceProc(hWnd, msg, wParam, lParam);
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}

	// ----------------------------------------------------------------------------
	// 实例消息处理
	// ----------------------------------------------------------------------------
	LRESULT ScreenshotApp::instanceProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_COMMAND: {
			UINT id = LOWORD(wParam);
			onTrayMenu(id);
			return 0;
		}

		case WM_HOTKEY: {
			int id = (int)wParam;
			if (id == HOTKEY_ID_REGION)      doCaptureRegion();
			else if (id == HOTKEY_ID_FULLSCREEN) doCaptureFullscreen();
			return 0;
		}

		case WM_ST_TRAYICON: {
			// lParam 表示事件类型
			if (lParam == WM_LBUTTONDBLCLK) {
				doCaptureRegion();
			}
			else if (lParam == WM_RBUTTONUP) {
				HMENU menu = CreatePopupMenu();
				AppendMenu(menu, MF_STRING, TrayMenuId::IDM_TRAY_CAPTURE_REGION, L"区域截图");
				AppendMenu(menu, MF_STRING, TrayMenuId::IDM_TRAY_CAPTURE_FULLSCREEN, L"全屏截图");
				AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
				AppendMenu(menu, MF_STRING, TrayMenuId::IDM_TRAY_OPEN_FOLDER, L"打开保存目录");
				AppendMenu(menu, MF_STRING, TrayMenuId::IDM_TRAY_TOGGLE_AUTOSTART, cfg_.autoStart ? L"取消开机启动" : L"设置开机启动");
				AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
				AppendMenu(menu, MF_STRING, TrayMenuId::IDM_TRAY_EXIT, L"退出");
				POINT pt; GetCursorPos(&pt);
				SetForegroundWindow(hWnd);
				TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
				DestroyMenu(menu);
			}
			return 0;
		}

		case WM_ST_REGION_DONE: {
			// Overlay 将 lParam 传递 RECT* 或 encoded rect，此处简化，RECT 直接拷贝
			RECT r = *reinterpret_cast<RECT*>(lParam); // TODO: 从 Overlay 实现获取
			onRegionSelected(r);
			return 0;
		}

		default: break;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
}