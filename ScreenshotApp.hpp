#pragma once
#include "common.hpp"
#include "HDRScreenCapture.hpp"
#include "SelectionOverlay.hpp"
#include "Config.hpp"
class ScreenshotApp {
private:
	HWND hwnd = nullptr;
	NOTIFYICONDATA nid{};
        // 不在此处持有 HDRScreenCapture，对象将在每次截图时临时创建
        std::unique_ptr<SelectionOverlay> overlay;
	Config config;

public:
	bool Initialize() {
		// 创建隐藏窗口
		WNDCLASS wc{
			.lpfnWndProc = WindowProc,
			.hInstance = GetModuleHandle(nullptr),
			.lpszClassName = L"HDRScreenshotApp"
		};

		RegisterClass(&wc);

		hwnd = CreateWindow(
			L"HDRScreenshotApp", L"HDR Screenshot Tool",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
			nullptr, nullptr, GetModuleHandle(nullptr), this);

		if (!hwnd) return false;

		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		// 加载配置
		LoadConfig();

                // HDRScreenCapture 不再在此持久化创建，改为截图时临时初始化

		// 创建选择覆盖窗口
		overlay = std::make_unique<SelectionOverlay>();
		if (!overlay->Create(hwnd)) {
			MessageBox(nullptr, L"Failed to create selection overlay", L"Error", MB_OK);
			return false;
		}

		// 创建系统托盘图标
		CreateTrayIcon();

		// 注册热键
		RegisterHotkeys();

		return true;
	}

	void Run() {
		MSG msg;
		while (GetMessage(&msg, nullptr, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	void Cleanup() {
		UnregisterHotKey(hwnd, WM_HOTKEY_REGION);
		UnregisterHotKey(hwnd, WM_HOTKEY_FULLSCREEN);
		Shell_NotifyIcon(NIM_DELETE, &nid);
		overlay.reset();
	}

private:
	void LoadConfig() {
		std::ifstream file("config.ini");
		if (!file.is_open()) {
			SaveConfig(); // 创建默认配置
			return;
		}

		std::string line;
		while (std::getline(file, line)) {
			if (line.empty() || line[0] == ';') continue;

			if (auto pos = line.find('='); pos != std::string::npos) {
				auto key = line.substr(0, pos);
				auto value = line.substr(pos + 1);

				if (key == "RegionHotkey") config.regionHotkey = value;
				else if (key == "FullscreenHotkey") config.fullscreenHotkey = value;
				else if (key == "SavePath") config.savePath = value;
				else if (key == "AutoStart") config.autoStart = (value == "true");
				else if (key == "SaveToFile") config.saveToFile = (value == "true");
				else if (key == "ShowNotification") config.showNotification = (value == "true");
                                else if (key == "DebugMode") config.debugMode = (value == "true");
                                else if (key == "UseACESFilmToneMapping") config.useACESFilmToneMapping = (value == "true");
                                else if (key == "SDRBrightness") config.sdrBrightness = std::stof(value);
                                else if (key == "FullscreenCurrentMonitor") config.fullscreenCurrentMonitor = (value == "true");
                                else if (key == "RegionFullscreenMonitor") config.regionFullscreenMonitor = (value == "true");
                                else if (key == "CaptureRetryCount") config.captureRetryCount = std::clamp(std::stoi(value), 1, 10);
                        }
                }
        }

	void SaveConfig() {
		std::ofstream file("config.ini");
		file << "; HDR Screenshot Tool Configuration\n"
			<< "; Basic hotkeys and paths\n"
			<< std::format("RegionHotkey={}\n", config.regionHotkey)
			<< std::format("FullscreenHotkey={}\n", config.fullscreenHotkey)
			<< std::format("SavePath={}\n", config.savePath)
			<< std::format("AutoStart={}\n", config.autoStart ? "true" : "false")
			<< std::format("SaveToFile={}\n", config.saveToFile ? "true" : "false")
                        << std::format("ShowNotification={}\n", config.showNotification ? "true" : "false")
                        << "\n; Debug settings\n"
                        << std::format("DebugMode={}\n", config.debugMode ? "true" : "false")
                        << "\n; HDR settings\n"
                        << std::format("UseACESFilmToneMapping={}\n", config.useACESFilmToneMapping ? "true" : "false")
                        << std::format("SDRBrightness={}\n", config.sdrBrightness)
                        << std::format("\nFullscreenCurrentMonitor={}\n", config.fullscreenCurrentMonitor ? "true" : "false")
                        << std::format("\nRegionFullscreenMonitor={}\n", config.regionFullscreenMonitor ? "true" : "false")
                        << std::format("\nCaptureRetryCount={}\n", config.captureRetryCount);
        }

	void CreateTrayIcon() {
		// 创建一个简单的图标
		auto icon = LoadIcon(nullptr, IDI_APPLICATION);

		nid = NOTIFYICONDATA{
			.cbSize = sizeof(nid),
			.hWnd = hwnd,
			.uID = IDI_TRAY_ICON,
			.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP,
			.uCallbackMessage = WM_TRAY_ICON,
			.hIcon = icon
		};

		wcscpy_s(nid.szTip, L"HDR Screenshot Tool");
		Shell_NotifyIcon(NIM_ADD, &nid);
	}

	void RegisterHotkeys() {
		// 解析热键字符串并注册
		if (auto [mod1, vk1] = ParseHotkey(config.regionHotkey); vk1 != 0) {
			RegisterHotKey(hwnd, WM_HOTKEY_REGION, mod1, vk1);
		}
		if (auto [mod2, vk2] = ParseHotkey(config.fullscreenHotkey); vk2 != 0) {
			RegisterHotKey(hwnd, WM_HOTKEY_FULLSCREEN, mod2, vk2);
		}
	}

        std::pair<UINT, UINT> ParseHotkey(std::string_view hotkey) {
                UINT modifiers = 0;
                UINT vkey = 0;

		auto lower = std::string(hotkey);
		std::ranges::transform(lower, lower.begin(), ::tolower);

		if (lower.find("ctrl") != std::string::npos) modifiers |= MOD_CONTROL;
		if (lower.find("shift") != std::string::npos) modifiers |= MOD_SHIFT;
		if (lower.find("alt") != std::string::npos) modifiers |= MOD_ALT;

		// 简单的键码映射
		if (lower.find("+a") != std::string::npos) vkey = 'A';
		else if (lower.find("+s") != std::string::npos) vkey = 'S';
                else if (lower.find("+d") != std::string::npos) vkey = 'D';

                return { modifiers, vkey };
        }

        bool GetForegroundFullscreenRect(RECT& rect) {
                HWND fg = GetForegroundWindow();
                if (!fg || IsIconic(fg)) return false;
                if (overlay && fg == overlay->GetHwnd()) return false;

                HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{ sizeof(mi) };
                if (!GetMonitorInfo(mon, &mi)) return false;

                RECT wrect{};
                if (!GetWindowRect(fg, &wrect)) return false;

                LONG style = GetWindowLong(fg, GWL_STYLE);
                LONG exStyle = GetWindowLong(fg, GWL_EXSTYLE);
                bool hasDecoration = style & (WS_CAPTION | WS_THICKFRAME);
                bool isTopmost = exStyle & WS_EX_TOPMOST;

                if (!hasDecoration && isTopmost &&
                        wrect.left <= mi.rcMonitor.left && wrect.top <= mi.rcMonitor.top &&
                        wrect.right >= mi.rcMonitor.right && wrect.bottom >= mi.rcMonitor.bottom) {
                        rect = mi.rcMonitor;
                        return true;
                }
                return false;
        }

	void ShowTrayMenu() {
		auto menu = CreatePopupMenu();

		AppendMenu(menu, MF_STRING | (config.autoStart ? MF_CHECKED : MF_UNCHECKED),
			IDM_AUTOSTART, L"开机启动");
		AppendMenu(menu, MF_STRING | (config.saveToFile ? MF_CHECKED : MF_UNCHECKED),
			IDM_SAVE_TO_FILE, L"保存到文件");
		AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenu(menu, MF_STRING, IDM_RELOAD, L"重载配置");
		AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenu(menu, MF_STRING, IDM_EXIT, L"退出");

		POINT pt;
		GetCursorPos(&pt);
		SetForegroundWindow(hwnd);

		TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
		DestroyMenu(menu);
	}

	bool CreateShortcut(const std::wstring& shortcutPath) {
		ComPtr<IShellLink> pShellLink;
		HRESULT hr = CoCreateInstance(
			CLSID_ShellLink,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pShellLink)
		);

		if (SUCCEEDED(hr)) {
			// 获取当前程序路径
			wchar_t exePath[MAX_PATH];
			GetModuleFileName(nullptr, exePath, MAX_PATH);

			// 设置快捷方式属性
			pShellLink->SetPath(exePath);
			pShellLink->SetDescription(L"HDR Screenshot Tool");

			// 获取程序所在目录作为工作目录
			std::wstring workingDir = exePath;
			size_t lastSlash = workingDir.find_last_of(L'\\');
			if (lastSlash != std::wstring::npos) {
				workingDir = workingDir.substr(0, lastSlash);
				pShellLink->SetWorkingDirectory(workingDir.c_str());
			}

			// 保存快捷方式
			ComPtr<IPersistFile> pPersistFile;
			hr = pShellLink.As(&pPersistFile);

			if (SUCCEEDED(hr)) {
				hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
			}
		}

		return SUCCEEDED(hr);
	}

	void ToggleAutoStart() {
		config.autoStart = !config.autoStart;
		SaveConfig();

		// 获取启动文件夹路径
		wchar_t startupPath[MAX_PATH];
		if (SHGetSpecialFolderPath(nullptr, startupPath, CSIDL_STARTUP, FALSE)) {
			std::wstring shortcutPath = std::wstring(startupPath) + L"\\HDR Screenshot Tool.lnk";

			if (config.autoStart) {
				// 创建快捷方式
				CreateShortcut(shortcutPath);
			}
			else {
				// 删除快捷方式
				DeleteFile(shortcutPath.c_str());
			}
		}
	}


        void ToggleSaveToFile() {
                config.saveToFile = !config.saveToFile;
                SaveConfig();
        }

        template<class F>
        bool TryCapture(F&& func) {
                int retries = std::max(1, config.captureRetryCount);
                for (int i = 0; i < retries; ++i) {
                        if (func()) return true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                return false;
        }

        void TakeRegionScreenshot() {
                RECT full;
                if (GetForegroundFullscreenRect(full)) {
                        if (!config.regionFullscreenMonitor) return;

                        std::optional<std::string> filename;
                        if (config.saveToFile) {
                                CreateDirectoryW(std::wstring(config.savePath.begin(), config.savePath.end()).c_str(), nullptr);
                                auto now = system_clock::now();
                                auto time_t = system_clock::to_time_t(now);
                                std::tm tm; localtime_s(&tm, &time_t);
                                filename = std::format("{}/screenshot_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.png",
                                        config.savePath, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                        tm.tm_hour, tm.tm_min, tm.tm_sec);
                        }

                        HDRScreenCapture cap;
                        cap.SetConfig(&config);
                        if (!cap.Initialize()) {
                                ShowNotification(L"截图失败");
                                return;
                        }

                        int w = full.right - full.left;
                        int h = full.bottom - full.top;
                        if (TryCapture([&] { return cap.CaptureRegion(full.left, full.top, w, h, filename.value_or("")); })) {
                                if (config.saveToFile && filename) {
                                        ShowNotification(L"截图已保存", std::wstring(filename->begin(), filename->end()));
                                } else {
                                        ShowNotification(L"截图已复制到剪贴板");
                                }
                        } else {
                                ShowNotification(L"截图失败");
                        }
                } else {
                        overlay->Show();
                }
        }

	void TakeFullscreenScreenshot() {
		std::optional<std::string> filename;

		if (config.saveToFile) {
			CreateDirectoryW(std::wstring(config.savePath.begin(), config.savePath.end()).c_str(), nullptr);

			auto now = system_clock::now();
			auto time_t = system_clock::to_time_t(now);
			std::tm tm;
			localtime_s(&tm, &time_t);

			filename = std::format("{}/screenshot_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.png",
				config.savePath, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
		}

                bool success = false;
                HDRScreenCapture cap;
                cap.SetConfig(&config);
                if (!cap.Initialize()) {
                        ShowNotification(L"截图失败");
                        return;
                }
                if (config.fullscreenCurrentMonitor) {
                        POINT pt; GetCursorPos(&pt);
                        for (const auto& m : cap.GetMonitors()) {
                                if (PtInRect(&m.desktopRect, pt)) {
                                        int w = m.desktopRect.right - m.desktopRect.left;
                                        int h = m.desktopRect.bottom - m.desktopRect.top;
                                        success = TryCapture([&] { return cap.CaptureRegion(m.desktopRect.left, m.desktopRect.top, w, h, filename.value_or("") ); });
                                        break;
                                }
                        }
                } else {
                        success = TryCapture([&] { return cap.CaptureFullscreen(filename.value_or("")); });
                }

                if (success) {
                        if (config.saveToFile && filename) {
                                ShowNotification(L"全屏截图已保存", std::wstring(filename->begin(), filename->end()));
                        }
                        else {
                                ShowNotification(L"全屏截图已复制到剪贴板");
                        }
                }
                else {
                        ShowNotification(L"截图失败");
		}
	}

	void OnRegionSelected() {
		auto rect = overlay->selectedRect;
		int width = rect.right - rect.left;
		int height = rect.bottom - rect.top;

		if (width > 0 && height > 0) {
			std::optional<std::string> filename;

			if (config.saveToFile) {
				CreateDirectoryW(std::wstring(config.savePath.begin(), config.savePath.end()).c_str(), nullptr);

				auto now = system_clock::now();
				auto time_t = system_clock::to_time_t(now);
				std::tm tm;
				localtime_s(&tm, &time_t);

				filename = std::format("{}/screenshot_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.png",
					config.savePath, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
					tm.tm_hour, tm.tm_min, tm.tm_sec);
			}

                        int globalLeft = rect.left + GetSystemMetrics(SM_XVIRTUALSCREEN);
                        int globalTop = rect.top + GetSystemMetrics(SM_YVIRTUALSCREEN);

                        HDRScreenCapture cap;
                        cap.SetConfig(&config);
                        if (!cap.Initialize()) {
                                ShowNotification(L"截图失败");
                                return;
                        }

                        if (TryCapture([&] { return cap.CaptureRegion(globalLeft, globalTop, width, height, filename.value_or("")); })) {
                                if (config.saveToFile && filename) {
                                        ShowNotification(L"截图已保存", std::wstring(filename->begin(), filename->end()));
                                }
                                else {
                                        ShowNotification(L"截图已复制到剪贴板");
                                }
			}
			else {
				ShowNotification(L"截图失败");
			}
		}
	}

	void ShowNotification(const std::wstring& message,
		const std::optional<std::wstring>& path = std::nullopt) {

		if (!config.showNotification) {
			return;
		}

		// 确保托盘图标可以显示通知
		nid.uFlags = NIF_INFO;
		nid.dwInfoFlags = NIIF_INFO;

		std::wstring info = message;
		if (path && path->length() < 200) { // 避免路径过长
			info += L"\n" + *path;
		}

		// 确保消息不会太长
		if (info.length() > 255) {
			info = info.substr(0, 252) + L"...";
		}

		wcsncpy_s(nid.szInfo, info.c_str(), _TRUNCATE);
		wcscpy_s(nid.szInfoTitle, L"HDR Screenshot Tool");
		Shell_NotifyIcon(NIM_MODIFY, &nid);

		// 重置标志
		nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	}

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		auto* app = reinterpret_cast<ScreenshotApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		if (!app) return DefWindowProc(hwnd, msg, wParam, lParam);

		switch (msg) {
		case WM_TRAY_ICON:
			if (lParam == WM_RBUTTONUP) {
				app->ShowTrayMenu();
			}
			break;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
			case IDM_AUTOSTART:
				app->ToggleAutoStart();
				break;
			case IDM_SAVE_TO_FILE:
				app->ToggleSaveToFile();
				break;
			case IDM_RELOAD:
				app->LoadConfig();
				app->RegisterHotkeys();
				break;
			case IDM_EXIT:
				PostQuitMessage(0);
				break;
			}
			break;

                case WM_HOTKEY:
                        if (wParam == WM_HOTKEY_REGION) {
                                app->TakeRegionScreenshot();
                        }
                        else if (wParam == WM_HOTKEY_FULLSCREEN) {
                                app->TakeFullscreenScreenshot();
                        }
                        break;

                case WM_DISPLAYCHANGE:
                case WM_DEVICECHANGE:
                        // 截图时会重新初始化设备，此处无需处理
                        break;

                case WM_POWERBROADCAST:
                        // 休眠恢复后无需特别操作
                        break;

                case WM_USER + 100: // 区域选择完成
                        app->OnRegionSelected();
                        break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		return 0;
	}
};

