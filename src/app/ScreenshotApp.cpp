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
#include "../platform/WinGDIPlusInit.hpp"
#include "../capture/SmartCapture.hpp"
#include "../image/ImageBuffer.hpp" // 添加ImageBuffer头文件

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
	// 多显示器支持的工具函数
	// ----------------------------------------------------------------------------
	
	// 获取鼠标当前所在的显示器
	static RECT getCurrentMonitorRect() {
		POINT cursorPos;
		GetCursorPos(&cursorPos);
		HMONITOR hMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);
		
		MONITORINFO monInfo = { sizeof(MONITORINFO) };
		if (GetMonitorInfo(hMonitor, &monInfo)) {
			return monInfo.rcMonitor;
		}
		
		// 如果失败，返回主显示器
		RECT primaryRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		return primaryRect;
	}
	
	// 获取指定窗口所在的显示器
	static RECT getWindowMonitorRect(HWND hwnd) {
		HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		
		MONITORINFO monInfo = { sizeof(MONITORINFO) };
		if (GetMonitorInfo(hMonitor, &monInfo)) {
			return monInfo.rcMonitor;
		}
		
		// 如果失败，返回主显示器
		RECT primaryRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		return primaryRect;
	}
	
	// 枚举所有显示器的回调函数
	struct MonitorEnumData {
		std::vector<RECT> monitors;
		RECT targetRect;
		int bestIndex = -1;
		int minDistance = INT_MAX;
	};
	
	static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
		MonitorEnumData* data = reinterpret_cast<MonitorEnumData*>(dwData);
		
		MONITORINFO monInfo = { sizeof(MONITORINFO) };
		if (GetMonitorInfo(hMonitor, &monInfo)) {
			data->monitors.push_back(monInfo.rcMonitor);
			
			// 计算与目标区域的距离（用于找到最近的显示器）
			RECT intersection;
			if (IntersectRect(&intersection, &monInfo.rcMonitor, &data->targetRect)) {
				// 如果有交集，这是最好的选择
				data->bestIndex = static_cast<int>(data->monitors.size()) - 1;
				data->minDistance = 0;
			} else {
				// 计算中心点距离
				int monCenterX = (monInfo.rcMonitor.left + monInfo.rcMonitor.right) / 2;
				int monCenterY = (monInfo.rcMonitor.top + monInfo.rcMonitor.bottom) / 2;
				int targetCenterX = (data->targetRect.left + data->targetRect.right) / 2;
				int targetCenterY = (data->targetRect.top + data->targetRect.bottom) / 2;
				
				int distance = abs(monCenterX - targetCenterX) + abs(monCenterY - targetCenterY);
				if (distance < data->minDistance) {
					data->minDistance = distance;
					data->bestIndex = static_cast<int>(data->monitors.size()) - 1;
				}
			}
		}
		
		return TRUE;
	}
	
	// 根据区域找到最合适的显示器
	static RECT getBestMonitorForRect(const RECT& rect) {
		MonitorEnumData enumData;
		enumData.targetRect = rect;
		
		EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&enumData));
		
		if (enumData.bestIndex >= 0 && enumData.bestIndex < enumData.monitors.size()) {
			return enumData.monitors[enumData.bestIndex];
		}
		
		// 如果失败，返回主显示器
		RECT primaryRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		return primaryRect;
	}

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

		// 1) 加载配置，确保配置文件存在并包含所有配置项
		bool configLoaded = LoadConfig(cfg_, L"config.ini");
		if (!configLoaded) {
			Logger::Info(L"Config file not found, creating default config...");
			// 首次运行，保存默认配置
			if (SaveConfig(cfg_, L"config.ini")) {
				Logger::Info(L"Default config.ini created successfully.");
			} else {
				Logger::Warn(L"Failed to create default config.ini");
			}
		} else {
			Logger::Info(L"Config loaded from config.ini");
			// 确保配置文件包含所有最新配置项
			EnsureConfigFile(cfg_, L"config.ini");
		}

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
		// 使用默认应用程序图标，因为还没有自定义图标资源
		HICON defaultIcon = LoadIcon(nullptr, IDI_APPLICATION);
		if (!tray_.Create(hwnd_, WM_ST_TRAYICON, defaultIcon, L"HDR Screenshot Tool")) {
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

		// 9) 初始化显示配置监控
		lastDisplayWidth_ = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		lastDisplayHeight_ = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		lastDisplayChangeTime_ = GetTickCount();

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
			// 立刻保存配置变更
			if (SaveConfig(cfg_, L"config.ini")) {
				Logger::Info(L"AutoStart setting saved: {}", cfg_.autoStart ? L"enabled" : L"disabled");
			}
			break;
		case TrayMenuId::IDM_TRAY_TOGGLE_FULLSCREEN_CURRENT_MONITOR:
			cfg_.fullscreenCurrentMonitor = !cfg_.fullscreenCurrentMonitor;
			SaveConfig(cfg_, L"config.ini");
			Logger::Info(L"Fullscreen current monitor: {}", cfg_.fullscreenCurrentMonitor ? L"enabled" : L"disabled");
			break;
		case TrayMenuId::IDM_TRAY_TOGGLE_REGION_FULLSCREEN_MONITOR:
			cfg_.regionFullscreenMonitor = !cfg_.regionFullscreenMonitor;
			SaveConfig(cfg_, L"config.ini");
			Logger::Info(L"Region fullscreen monitor: {}", cfg_.regionFullscreenMonitor ? L"enabled" : L"disabled");
			break;
		case TrayMenuId::IDM_TRAY_TOGGLE_SAVEFILE:
			cfg_.saveToFile = !cfg_.saveToFile;
			SaveConfig(cfg_, L"config.ini");
			Logger::Info(L"Save to file: {}", cfg_.saveToFile ? L"enabled" : L"disabled");
			break;
		case TrayMenuId::IDM_TRAY_EXIT:
			PostMessage(hwnd_, WM_CLOSE, 0, 0);
			break;
		default: 
			break;
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
		
		// 确保捕获系统就绪，检测显示配置变化
		if (!ensureCaptureReady()) {
			Logger::Error(L"Failed to ensure capture system ready");
			return;
		}
		
		// 根据配置决定区域选择的范围
		RECT overlayRect;
		if (cfg_.regionFullscreenMonitor) {
			// 限制在当前显示器上进行区域选择
			overlayRect = getCurrentMonitorRect();
			Logger::Info(L"Region selection: limited to current monitor only {}x{} at ({}, {})", 
			            overlayRect.right - overlayRect.left, 
			            overlayRect.bottom - overlayRect.top,
			            overlayRect.left, overlayRect.top);
		} else {
			// 在整个虚拟桌面上进行区域选择（支持跨显示器）
			overlayRect = capture_.GetVirtualDesktop();
			Logger::Info(L"Region selection: cross-monitor support on virtual desktop {}x{} at ({}, {})",
			            overlayRect.right - overlayRect.left, 
			            overlayRect.bottom - overlayRect.top,
			            overlayRect.left, overlayRect.top);
		}
		
		// 先捕获相应区域的数据到缓存（确保overlay不在屏幕上）
		// 在捕获前确保没有任何overlay窗口可见
		if (!capture_.CaptureFullscreenToCache()) {
			Logger::Error(L"Failed to cache fullscreen for region selection");
			return;
		}
		
		// 将缓存的图像数据传递给overlay用作背景
		ImageBuffer rgb8Image;
		if (capture_.GetCachedImageAsRGB8(rgb8Image)) {
			Logger::Info(L"Setting background image for overlay: {}x{}", rgb8Image.width, rgb8Image.height);
			overlay_.SetBackgroundImage(rgb8Image.data.data(), 
			                          rgb8Image.width, 
			                          rgb8Image.height, 
			                          rgb8Image.stride);
		} else {
			Logger::Warn(L"Failed to get RGB8 cached image for overlay background");
			overlay_.SetBackgroundImage(nullptr, 0, 0, 0); // 清除背景
		}
		
		// 然后显示 overlay 进行区域选择
		// 如果支持多显示器区域选择，可以传递overlayRect给overlay
		if (cfg_.regionFullscreenMonitor) {
            overlay_.BeginSelectOnMonitor(overlayRect);
        } else {
            overlay_.BeginSelect();
        }
	}

	// ----------------------------------------------------------------------------
	// 全屏截图
	// ----------------------------------------------------------------------------
	void ScreenshotApp::doCaptureFullscreen() {
		RECT captureRect;
		
		if (cfg_.fullscreenCurrentMonitor) {
			// 仅捕获鼠标当前所在的显示器
			captureRect = getCurrentMonitorRect();
			Logger::Info(L"Fullscreen capture: current monitor only {}x{} at ({}, {})", 
			            captureRect.right - captureRect.left, 
			            captureRect.bottom - captureRect.top,
			            captureRect.left, captureRect.top);
		} else {
			// 捕获整个虚拟桌面（所有显示器）
			captureRect = capture_.GetVirtualDesktop();
			Logger::Info(L"Fullscreen capture: all monitors (virtual desktop) {}x{} at ({}, {})", 
			            captureRect.right - captureRect.left, 
			            captureRect.bottom - captureRect.top,
			            captureRect.left, captureRect.top);
		}
		
		// 确保捕获系统就绪，检测显示配置变化
		if (!ensureCaptureReady()) {
			Logger::Error(L"Failed to ensure capture system ready");
			return;
		}
		
		// 直接执行全屏截图 - 不使用缓存，立即捕获
		CaptureRectDirect(captureRect);
	}

	// ----------------------------------------------------------------------------
	// Overlay 区域选择 -> App 接收 WM_ST_REGION_DONE
	// ----------------------------------------------------------------------------
	void ScreenshotApp::onRegionSelected(const RECT& r) {
		Logger::Info(L">>> onRegionSelected called with rect: ({},{}) to ({},{})", 
		            r.left, r.top, r.right, r.bottom);
		CaptureRect(r);
	}

	// ----------------------------------------------------------------------------
	// 实现执行截图逻辑，支持全屏
	// ----------------------------------------------------------------------------
	void ScreenshotApp::CaptureRect(const RECT& r) {
		Logger::Info(L">>> CaptureRect called with rect: ({},{}) to ({},{})", 
		            r.left, r.top, r.right, r.bottom);
		
		std::wstring savePath = ensureSaveDir(cfg_);
		std::wstring filename = PathUtils::MakeTimestampedPngNameW();
		
		// 构建完整的文件路径
		std::wstring fullPath = savePath;
		if (!fullPath.empty() && fullPath.back() != L'\\') {
			fullPath += L'\\';
		}
		fullPath += filename;

		Logger::Info(L"Saving screenshot to: {}", fullPath);

		// 使用冻结帧数据进行区域提取
		SmartCapture::Result res = capture_.ExtractRegionFromCache(hwnd_, r, cfg_.saveToFile ? fullPath.c_str() : nullptr);
		
		Logger::Info(L"Screenshot capture result: {}", static_cast<int>(res));
		
		switch (res) {
		case SmartCapture::Result::OK:
			Logger::Info(L"Screenshot saved successfully: {}", filename);
			break;
		case SmartCapture::Result::FallbackGDI:
			Logger::Info(L"Screenshot saved using GDI fallback: {}", filename);
			break;
		default:
			Logger::Error(L"Screenshot failed");
			break;
		}
		
		Logger::Info(L"<<< CaptureRect finished");
	}
	
	// ----------------------------------------------------------------------------
	// 直接捕获指定区域（不依赖缓存，用于全屏截图）
	// ----------------------------------------------------------------------------
	void ScreenshotApp::CaptureRectDirect(const RECT& r) {
		Logger::Info(L">>> CaptureRectDirect called with rect: ({},{}) to ({},{})", 
		            r.left, r.top, r.right, r.bottom);
		
		std::wstring savePath = ensureSaveDir(cfg_);
		std::wstring filename = PathUtils::MakeTimestampedPngNameW();
		
		// 构建完整的文件路径
		std::wstring fullPath = savePath;
		if (!fullPath.empty() && fullPath.back() != L'\\') {
			fullPath += L'\\';
		}
		fullPath += filename;

		Logger::Info(L"Saving screenshot to: {}", fullPath);

		// 直接捕获指定区域
		SmartCapture::Result res = capture_.CaptureToFileAndClipboard(hwnd_, r, cfg_.saveToFile ? fullPath.c_str() : nullptr);
		
		Logger::Info(L"Screenshot capture result: {}", static_cast<int>(res));
		
		switch (res) {
		case SmartCapture::Result::OK:
			Logger::Info(L"Screenshot saved successfully: {}", filename);
			break;
		case SmartCapture::Result::FallbackGDI:
			Logger::Info(L"Screenshot saved using GDI fallback: {}", filename);
			break;
		default:
			Logger::Error(L"Screenshot failed");
			break;
		}
		
		Logger::Info(L"<<< CaptureRectDirect finished");
	}

	// ----------------------------------------------------------------------------
	// 确保捕获系统就绪，检测显示配置变化
	// ----------------------------------------------------------------------------
	bool ScreenshotApp::ensureCaptureReady() {
		// 检查显示配置是否发生变化
		UINT currentWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		UINT currentHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		DWORD currentTime = GetTickCount();
		
		bool displayChanged = (currentWidth != lastDisplayWidth_ || 
		                      currentHeight != lastDisplayHeight_);
		
		if (displayChanged) {
			Logger::Info(L"Display configuration changed: {}x{} -> {}x{}", 
			            lastDisplayWidth_, lastDisplayHeight_, 
			            currentWidth, currentHeight);
			
			lastDisplayWidth_ = currentWidth;
			lastDisplayHeight_ = currentHeight;
			lastDisplayChangeTime_ = currentTime;
			
			// 显示配置变化，强制重新初始化
			return capture_.Initialize();
		}
		
		// 检查是否需要重新初始化
		if (!capture_.Initialize()) {
			Logger::Warn(L"Capture initialization failed, will rely on fallback");
		}
		
		return true;
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
                
                // 多显示器选项子菜单
                HMENU multiMonitorMenu = CreatePopupMenu();
                AppendMenu(multiMonitorMenu, MF_STRING | (cfg_.fullscreenCurrentMonitor ? MF_CHECKED : MF_UNCHECKED), 
                          TrayMenuId::IDM_TRAY_TOGGLE_FULLSCREEN_CURRENT_MONITOR, L"全屏截图仅当前显示器");
                AppendMenu(multiMonitorMenu, MF_STRING | (cfg_.regionFullscreenMonitor ? MF_CHECKED : MF_UNCHECKED), 
                          TrayMenuId::IDM_TRAY_TOGGLE_REGION_FULLSCREEN_MONITOR, L"区域选择限制当前显示器");
                AppendMenu(menu, MF_POPUP, (UINT_PTR)multiMonitorMenu, L"多显示器设置");
                
                AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenu(menu, MF_STRING, TrayMenuId::IDM_TRAY_OPEN_FOLDER, L"打开保存目录");
                AppendMenu(menu, MF_STRING | (cfg_.saveToFile ? MF_CHECKED : MF_UNCHECKED), 
                          TrayMenuId::IDM_TRAY_TOGGLE_SAVEFILE, L"保存到文件");
                AppendMenu(menu, MF_STRING | (cfg_.autoStart ? MF_CHECKED : MF_UNCHECKED), 
                          TrayMenuId::IDM_TRAY_TOGGLE_AUTOSTART, L"开机启动");
                AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenu(menu, MF_STRING, TrayMenuId::IDM_TRAY_EXIT, L"退出");
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hWnd);
                TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
                DestroyMenu(multiMonitorMenu);
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