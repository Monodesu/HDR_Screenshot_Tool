#include "../platform/WinHeaders.hpp"
#include "ScreenshotApp.hpp"

#include "../config/Config.hpp"
#include "../util/Logger.hpp"
#include "../util/PathUtils.hpp"
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
// 本地常量 & 宏
// ============================================================================
namespace screenshot_tool {

    // 自定义托盘图标消息 ID
    static constexpr UINT WM_ST_TRAYICON = WM_APP + 1;
    // 自定义 Overlay 与 App 通信（Overlay 完成选择时 PostMessage）
    static constexpr UINT WM_ST_REGION_DONE = WM_APP + 2;

    // 托盘菜单命令 ID
    enum : UINT {
        IDM_TRAY_CAPTURE_REGION = 1001,
        IDM_TRAY_CAPTURE_FULLSCREEN,
        IDM_TRAY_OPEN_FOLDER,
        IDM_TRAY_TOGGLE_AUTOSTART,
        IDM_TRAY_EXIT
    };

    // 热键 ID（与 HotkeyManager 内部保持一致或映射）
    static constexpr int HOTKEY_ID_REGION = 1;
    static constexpr int HOTKEY_ID_FULLSCREEN = 2;

    // ----------------------------------------------------------------------------
    // 工具：创建屏幕截图保存目录（如果不存在则自动创建）
    // ----------------------------------------------------------------------------
    static std::wstring ensureSaveDir(const Config& cfg) {
        std::wstring dirW = PathUtils::Utf8ToWide(cfg.savePath.c_str());
        if (dirW.empty()) {
            // 默认：当前工作目录下 Screenshots
            dirW = L"Screenshots";
        }
        if (!PathUtils::IsAbsolute(dirW)) {
            std::wstring exeDir = PathUtils::GetModuleDirectoryW();
            PathUtils::JoinInplace(exeDir, dirW); // exeDir += L"\\" + dirW
            dirW = exeDir;
        }
        PathUtils::CreateDirectoriesRecursive(dirW.c_str());
        return dirW;
    }

    // ----------------------------------------------------------------------------
    // ScreenshotApp 实现
    // ----------------------------------------------------------------------------
    ScreenshotApp::ScreenshotApp() : capture_(&cfg_) {}
    {
    }

    ScreenshotApp::~ScreenshotApp() {
        Shutdown();
    }

    bool ScreenshotApp::Initialize(HINSTANCE hInst) {
        hInst_ = hInst;

        // 1) 加载配置
        LoadConfig(cfg_); // Config.cpp 提供实现
        HDRSHOT_LOG_INFO("Config loaded.");

        // 2) 自动创建保存目录（根据配置）
        ensureSaveDir(cfg_);

        // 3) 注册窗口类 & 创建隐藏主窗口（用于消息分发 / 托盘 / 热键）
        const wchar_t* kClassName = L"HDRScreenshotAppWnd";
        WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ScreenshotApp::WndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = sizeof(LONG_PTR);
        wc.hInstance = hInst_;
        wc.hIcon = LoadIcon(hInst_, MAKEINTRESOURCE(1)); // 资源中第1号图标占位
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = kClassName;
        wc.hIconSm = LoadIcon(hInst_, MAKEINTRESOURCE(1));
        if (!RegisterClassEx(&wc)) {
            HDRSHOT_LOG_ERROR("RegisterClassEx failed");
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
            HDRSHOT_LOG_ERROR("CreateWindowEx failed");
            return false;
        }

        // 4) 初始化托盘图标
        if (!tray_.Initialize(hwnd_, WM_ST_TRAYICON, L"HDR Screenshot Tool")) {
            HDRSHOT_LOG_ERROR("Tray icon init failed");
        }
        else {
            tray_.Show();
        }

        // 5) 热键注册
        hotkeys_.Attach(hwnd_);
        if (!hotkeys_.RegisterFromString(HOTKEY_ID_REGION, cfg_.regionHotkey)) {
            HDRSHOT_LOG_WARN("Register region hotkey failed: {}", cfg_.regionHotkey);
        }
        if (!hotkeys_.RegisterFromString(HOTKEY_ID_FULLSCREEN, cfg_.fullscreenHotkey)) {
            HDRSHOT_LOG_WARN("Register fullscreen hotkey failed: {}", cfg_.fullscreenHotkey);
        }

        // 6) Overlay（区域选择）
        if (!overlay_.Create(hInst_, hwnd_, WM_ST_REGION_DONE)) {
            HDRSHOT_LOG_WARN("Overlay create failed (region capture disabled)");
        }

        // 7) Capture 管线初始化（底层 DXGI + GDI 回退）
        if (!capture_.Initialize()) {
            HDRSHOT_LOG_WARN("Capture init failed; will rely on GDI fallback");
        }

        // 8) 自动启动（若配置为 true）
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

        hotkeys_.Detach();
        tray_.Destroy();

        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }

        SaveConfig(cfg_);
        HDRSHOT_LOG_INFO("App shutdown.");
    }

    // ----------------------------------------------------------------------------
    // 托盘菜单点击处理
    // ----------------------------------------------------------------------------
    void ScreenshotApp::onTrayMenu(UINT cmd) {
        switch (cmd) {
        case IDM_TRAY_CAPTURE_REGION:     doCaptureRegion();      break;
        case IDM_TRAY_CAPTURE_FULLSCREEN: doCaptureFullscreen();  break;
        case IDM_TRAY_OPEN_FOLDER: {
            auto dirW = ensureSaveDir(cfg_);
            ShellExecute(nullptr, L"open", dirW.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case IDM_TRAY_TOGGLE_AUTOSTART:
            cfg_.autoStart = !cfg_.autoStart;
            applyAutoStart();
            break;
        case IDM_TRAY_EXIT:
            PostMessage(hwnd_, WM_CLOSE, 0, 0);
            break;
        default: break;
        }
    }

    // ----------------------------------------------------------------------------
    // 区域截图触发
    // ----------------------------------------------------------------------------
    void ScreenshotApp::doCaptureRegion() {
        if (!overlay_.IsValid()) {
            HDRSHOT_LOG_WARN("Region overlay invalid");
            return;
        }
        overlay_.BeginSelect();
    }

    // ----------------------------------------------------------------------------
    // 全屏截图触发
    // ----------------------------------------------------------------------------
    void ScreenshotApp::doCaptureFullscreen() {
        // TODO: 根据配置抓当前显示器或虚拟全部
        RECT vr = capture_.GetVirtualDesktop();
        CaptureRect(vr); // 临时调用同接口；后面拆分
    }

    // ----------------------------------------------------------------------------
    // Overlay 完成区域选择 -> App 收到 WM_ST_REGION_DONE
    // ----------------------------------------------------------------------------
    void ScreenshotApp::onRegionSelected(const RECT& r) {
        CaptureRect(r);
    }

    // ----------------------------------------------------------------------------
    // 实际执行截图（区域或全屏）
    // ----------------------------------------------------------------------------
    void ScreenshotApp::CaptureRect(const RECT& r) {
        std::wstring savePath = ensureSaveDir(cfg_);
        std::wstring filename = PathUtils::Join(savePath, TimeUtils::MakeTimestampFilenameW(L"HDRShot", L".png"));

        SmartCapture::Result res = capture_.CaptureToFileAndClipboard(r, filename.c_str());
        switch (res) {
        case SmartCapture::Result::OK:
            if (cfg_.showNotification) {
                WinNotification::ShowBalloon(hwnd_, L"截图已保存", filename.c_str());
            }
            break;
        case SmartCapture::Result::FallbackGDI:
            if (cfg_.showNotification) {
                WinNotification::ShowBalloon(hwnd_, L"DXGI 失败，已使用 GDI", filename.c_str());
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
    // 应用自动启动设置（创建/删除快捷方式）
    // ----------------------------------------------------------------------------
    void ScreenshotApp::applyAutoStart() {
        if (cfg_.autoStart) {
            WinShell::CreateStartupShortcut();
        }
        else {
            WinShell::RemoveStartupShortcut();
        }
    }

    // ----------------------------------------------------------------------------
    // Window Proc 桥接静态 -> 实例
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
            // lParam 区分点击类型
            if (lParam == WM_LBUTTONDBLCLK) {
                doCaptureRegion();
            }
            else if (lParam == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenu(menu, MF_STRING, IDM_TRAY_CAPTURE_REGION, L"区域截图");
                AppendMenu(menu, MF_STRING, IDM_TRAY_CAPTURE_FULLSCREEN, L"全屏截图");
                AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenu(menu, MF_STRING, IDM_TRAY_OPEN_FOLDER, L"打开保存目录");
                AppendMenu(menu, MF_STRING, IDM_TRAY_TOGGLE_AUTOSTART, cfg_.autoStart ? L"取消开机自启" : L"启用开机自启");
                AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenu(menu, MF_STRING, IDM_TRAY_EXIT, L"退出");
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hWnd);
                TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
                DestroyMenu(menu);
            }
            return 0;
        }

        case WM_ST_REGION_DONE: {
            // Overlay 在 lParam 传递 RECT* 或 encoded rect；此处简化：RECT 直接复制
            RECT r = *reinterpret_cast<RECT*>(lParam); // TODO: 按 Overlay 实现调整
            onRegionSelected(r);
            return 0;
        }

        default: break;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }