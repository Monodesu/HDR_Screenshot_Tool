#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <gdiplus.h>

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cmath>
#include <memory>
#include <optional>
#include <ranges>
#include <format>
#include <string_view>
#include <span>
#include <chrono>
#include <algorithm>
#include <cwchar>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

using Microsoft::WRL::ComPtr;
using namespace Gdiplus;
using namespace std::string_literals;
using namespace std::chrono;

// 资源ID定义
#define IDI_TRAY_ICON 101
#define IDM_AUTOSTART 102
#define IDM_SAVE_TO_FILE 103
#define IDM_RELOAD    104
#define IDM_EXIT      105
#define WM_TRAY_ICON  (WM_USER + 1)
#define WM_HOTKEY_REGION 1001
#define WM_HOTKEY_FULLSCREEN 1002

// 配置结构
struct Config {
    std::string regionHotkey = "ctrl+alt+a";
    std::string fullscreenHotkey = "ctrl+shift+alt+a";
    std::string savePath = "Screenshots";
    bool autoStart = false;
    bool saveToFile = true;
};

// HDR截图类
class HDRScreenCapture {
private:
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> deskDupl;
    ComPtr<IDXGIOutput6> output6;
    int screenWidth = 0;
    int screenHeight = 0;

public:
    bool Initialize() {
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &d3dDevice, &featureLevel, &d3dContext);

        if (FAILED(hr)) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice.As(&dxgiDevice);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIOutput> output;
        hr = adapter->EnumOutputs(0, &output);
        if (FAILED(hr)) return false;

        hr = output.As(&output6);
        if (FAILED(hr)) return false;

        DXGI_OUTPUT_DESC outputDesc;
        output6->GetDesc(&outputDesc);
        screenWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        screenHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

        hr = output6->DuplicateOutput(d3dDevice.Get(), &deskDupl);
        return SUCCEEDED(hr);
    }

    bool CaptureRegion(int x, int y, int width, int height, const std::string& filename = "") {
        ComPtr<IDXGIResource> resource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;

        HRESULT hr = deskDupl->AcquireNextFrame(1000, &frameInfo, &resource);
        if (FAILED(hr)) return false;

        auto cleanup = [&](void*) { deskDupl->ReleaseFrame(); };
        std::unique_ptr<void, decltype(cleanup)> frameGuard(reinterpret_cast<void*>(1), cleanup);

        ComPtr<ID3D11Texture2D> texture;
        hr = resource.As(&texture);
        if (FAILED(hr)) return false;

        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        // 创建区域纹理
        D3D11_TEXTURE2D_DESC regionDesc = desc;
        regionDesc.Width = width;
        regionDesc.Height = height;
        regionDesc.Usage = D3D11_USAGE_STAGING;
        regionDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        regionDesc.BindFlags = 0;
        regionDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> regionTexture;
        hr = d3dDevice->CreateTexture2D(&regionDesc, nullptr, &regionTexture);
        if (FAILED(hr)) return false;

        // 复制区域
        D3D11_BOX srcBox{
            .left = static_cast<UINT>(x),
            .top = static_cast<UINT>(y),
            .front = 0,
            .right = static_cast<UINT>(x + width),
            .bottom = static_cast<UINT>(y + height),
            .back = 1
        };

        d3dContext->CopySubresourceRegion(
            regionTexture.Get(), 0, 0, 0, 0,
            texture.Get(), 0, &srcBox);

        // 映射并保存
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3dContext->Map(regionTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return false;

        auto unmapGuard = [&](void*) { d3dContext->Unmap(regionTexture.Get(), 0); };
        std::unique_ptr<void, decltype(unmapGuard)> mapGuard(reinterpret_cast<void*>(1), unmapGuard);

        return ProcessAndSave(
            static_cast<uint8_t*>(mapped.pData), width, height,
            mapped.RowPitch, desc.Format, filename);
    }

    bool CaptureFullscreen(const std::string& filename = "") {
        return CaptureRegion(0, 0, screenWidth, screenHeight, filename);
    }

private:
    bool ProcessAndSave(uint8_t* data, int width, int height, int pitch,
        DXGI_FORMAT format, const std::string& filename) {

        std::vector<uint8_t> rgbBuffer(width * height * 3);

        switch (format) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            ProcessHDR16Float(data, rgbBuffer.data(), width, height, pitch);
            break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            ProcessHDR10(data, rgbBuffer.data(), width, height, pitch);
            break;
        default:
            ProcessSDR(data, rgbBuffer.data(), width, height, pitch);
            break;
        }

        // 保存到剪贴板
        bool clipboardSuccess = SaveToClipboard(rgbBuffer, width, height);

        // 如果需要保存到文件
        bool fileSuccess = true;
        if (!filename.empty()) {
            fileSuccess = SavePNG(rgbBuffer, width, height, filename);
        }

        return clipboardSuccess && fileSuccess;
    }

    void ProcessHDR16Float(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
        for (int y : std::views::iota(0, height)) {
            auto* srcRow = reinterpret_cast<uint16_t*>(src + y * pitch);
            auto* dstRow = dst + y * width * 3;

            for (int x : std::views::iota(0, width)) {
                float r = HalfToFloat(srcRow[x * 4 + 0]);
                float g = HalfToFloat(srcRow[x * 4 + 1]);
                float b = HalfToFloat(srcRow[x * 4 + 2]);

                Rec2020ToSRGB(r, g, b);

                r = ACESFilm(r);
                g = ACESFilm(g);
                b = ACESFilm(b);

                r = GammaCorrect(std::clamp(r, 0.0f, 1.0f));
                g = GammaCorrect(std::clamp(g, 0.0f, 1.0f));
                b = GammaCorrect(std::clamp(b, 0.0f, 1.0f));

                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255);
            }
        }
    }

    void ProcessHDR10(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
        for (int y : std::views::iota(0, height)) {
            auto* srcRow = reinterpret_cast<uint32_t*>(src + y * pitch);
            auto* dstRow = dst + y * width * 3;

            for (int x : std::views::iota(0, width)) {
                uint32_t pixel = srcRow[x];
                uint32_t r10 = (pixel >> 20) & 0x3FF;
                uint32_t g10 = (pixel >> 10) & 0x3FF;
                uint32_t b10 = pixel & 0x3FF;

                float r = PQToLinear(static_cast<float>(r10) / 1023.0f) / 1000.0f;
                float g = PQToLinear(static_cast<float>(g10) / 1023.0f) / 1000.0f;
                float b = PQToLinear(static_cast<float>(b10) / 1023.0f) / 1000.0f;

                Rec2020ToSRGB(r, g, b);

                r = ACESFilm(r);
                g = ACESFilm(g);
                b = ACESFilm(b);

                r = GammaCorrect(std::clamp(r, 0.0f, 1.0f));
                g = GammaCorrect(std::clamp(g, 0.0f, 1.0f));
                b = GammaCorrect(std::clamp(b, 0.0f, 1.0f));

                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255);
            }
        }
    }

    void ProcessSDR(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
        for (int y : std::views::iota(0, height)) {
            auto* srcRow = src + y * pitch;
            auto* dstRow = dst + y * width * 3;

            for (int x : std::views::iota(0, width)) {
                dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // R
                dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
                dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // B
            }
        }
    }

    static constexpr float ACESFilm(float x) noexcept {
        constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        return std::max(0.0f, (x * (a * x + b)) / (x * (c * x + d) + e));
    }

    static float GammaCorrect(float x) noexcept {
        return std::pow(x, 1.0f / 2.2f);
    }

    static void Rec2020ToSRGB(float& r, float& g, float& b) noexcept {
        float r2 = 1.66032f * r - 0.58757f * g - 0.07291f * b;
        float g2 = -0.12441f * r + 1.13280f * g - 0.00835f * b;
        float b2 = -0.01811f * r - 0.10060f * g + 1.11877f * b;
        r = r2;
        g = g2;
        b = b2;
    }

    static float PQToLinear(float pq) noexcept {
        // ST2084 EOTF from normalized PQ value to absolute nits
        constexpr float m1 = 0.1593017578125f;  // 2610/16384
        constexpr float m2 = 78.84375f;         // 2523/32
        constexpr float c1 = 0.8359375f;        // 3424/4096
        constexpr float c2 = 18.8515625f;       // 2413/128
        constexpr float c3 = 18.6875f;          // 2392/128

        pq = std::clamp(pq, 0.0f, 1.0f);
        float p = std::pow(pq, 1.0f / m2);
        float num = std::max(p - c1, 0.0f);
        float den = c2 - c3 * p;
        return std::pow(num / den, 1.0f / m1); // result in nits (0-10000)
    }

    static constexpr float HalfToFloat(uint16_t half) noexcept {
        uint32_t sign = (half >> 15) & 0x1;
        uint32_t exp = (half >> 10) & 0x1F;
        uint32_t mantissa = half & 0x3FF;

        if (exp == 0) {
            if (mantissa == 0) return sign ? -0.0f : 0.0f;
            float value = mantissa / 1024.0f;
            return sign ? -value * std::pow(2.0f, -14.0f) : value * std::pow(2.0f, -14.0f);
        }
        else if (exp == 31) {
            return sign ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
        }
        else {
            float value = (1.0f + mantissa / 1024.0f) * std::pow(2.0f, exp - 15);
            return sign ? -value : value;
        }
    }

    bool SavePNG(std::span<const uint8_t> data, int width, int height, const std::string& filename) {
        Bitmap bitmap(width, height, PixelFormat24bppRGB);
        BitmapData bitmapData;
        Rect rect(0, 0, width, height);

        if (bitmap.LockBits(&rect, ImageLockModeWrite, PixelFormat24bppRGB, &bitmapData) == Ok) {
            auto* dst = static_cast<uint8_t*>(bitmapData.Scan0);
            for (int y : std::views::iota(0, height)) {
                auto* dstRow = dst + y * bitmapData.Stride;
                auto* srcRow = data.data() + y * width * 3;
                for (int x : std::views::iota(0, width)) {
                    dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
                    dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
                    dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
                }
            }
            bitmap.UnlockBits(&bitmapData);

            if (!filename.empty()) {
                CLSID pngClsid;
                GetEncoderClsid(L"image/png", &pngClsid);

                auto wfilename = std::wstring(filename.begin(), filename.end());
                return bitmap.Save(wfilename.c_str(), &pngClsid, nullptr) == Ok;
            }

            return true;
        }
        return false;
    }

    bool SaveToClipboard(std::span<const uint8_t> data, int width, int height) {

        // 创建DIB数据
        int imageSize = width * height * 3;
        int dibSize = sizeof(BITMAPINFOHEADER) + imageSize;

        auto hDib = GlobalAlloc(GMEM_MOVEABLE, dibSize);
        if (!hDib) return false;

        auto dibGuard = [hDib](void*) { GlobalFree(hDib); };
        std::unique_ptr<void, decltype(dibGuard)> guard(reinterpret_cast<void*>(1), dibGuard);

        auto* bih = static_cast<BITMAPINFOHEADER*>(GlobalLock(hDib));
        if (!bih) return false;

        auto unlockGuard = [hDib](void*) { GlobalUnlock(hDib); };
        std::unique_ptr<void, decltype(unlockGuard)> lockGuard(reinterpret_cast<void*>(1), unlockGuard);

        // 填充位图信息头
        *bih = BITMAPINFOHEADER{
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = width,
            .biHeight = -height, // 负数表示从上到下
            .biPlanes = 1,
            .biBitCount = 24,
            .biCompression = BI_RGB,
            .biSizeImage = static_cast<DWORD>(imageSize)
        };

        // 复制图像数据（RGB转BGR）
        auto* dibData = reinterpret_cast<uint8_t*>(bih + 1);
        for (int y : std::views::iota(0, height)) {
            for (int x : std::views::iota(0, width)) {
                int srcIdx = (y * width + x) * 3;
                int dstIdx = (y * width + x) * 3;
                dibData[dstIdx + 0] = data[srcIdx + 2]; // B
                dibData[dstIdx + 1] = data[srcIdx + 1]; // G
                dibData[dstIdx + 2] = data[srcIdx + 0]; // R
            }
        }

        lockGuard.reset();

        // 打开剪贴板并设置数据
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            SetClipboardData(CF_DIB, hDib);
            CloseClipboard();
            guard.release(); // 成功时不释放内存
            return true;
        }

        return false;
    }

    static bool GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
        UINT num = 0, size = 0;
        GetImageEncodersSize(&num, &size);
        if (size == 0) return false;

        auto pImageCodecInfo = std::make_unique<uint8_t[]>(size);
        auto* codecInfo = reinterpret_cast<ImageCodecInfo*>(pImageCodecInfo.get());

        GetImageEncoders(num, size, codecInfo);

        for (UINT j : std::views::iota(0u, num)) {
            if (wcscmp(codecInfo[j].MimeType, format) == 0) {
                *pClsid = codecInfo[j].Clsid;
                return true;
            }
        }

        return false;
    }
};

// 区域选择覆盖窗口
class SelectionOverlay {
private:
    HWND hwnd = nullptr;
    HWND messageWnd = nullptr;
    BYTE alpha = 0;
    bool fadingIn = false;
    bool fadingOut = false;
    bool notifyHide = false;
    bool isSelecting = false;
    POINT startPoint{}, endPoint{};

public:
    RECT selectedRect{};

    bool Create(HWND msgWnd) {
        messageWnd = msgWnd;
        WNDCLASS wc{
            .lpfnWndProc = WindowProc,
            .hInstance = GetModuleHandle(nullptr),
            .hCursor = LoadCursor(nullptr, IDC_CROSS),
            .hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)),
            .lpszClassName = L"SelectionOverlay"
        };

        RegisterClass(&wc);

        hwnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
            L"SelectionOverlay", L"",
            WS_POPUP,
            0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
            nullptr, nullptr, GetModuleHandle(nullptr), this);

        if (!hwnd) return false;

        ShowWindow(hwnd, SW_HIDE);
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        return true;
    }

    void Show() {
        auto style = GetWindowLong(hwnd, GWL_EXSTYLE);
        SetWindowLong(hwnd, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
        isSelecting = false;
        startPoint = endPoint = POINT{};
        alpha = 0;
        fadingOut = false;
        fadingIn = true;
        KillTimer(hwnd, 2);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetTimer(hwnd, 1, 15, nullptr);
    }

    void Hide(bool notify = false) {
        if (!IsWindowVisible(hwnd)) return;
        notifyHide = notify;
        fadingIn = false;
        fadingOut = true;
        KillTimer(hwnd, 1);
        SetTimer(hwnd, 2, 15, nullptr);
    }

    void Destroy() {
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* overlay = reinterpret_cast<SelectionOverlay*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!overlay) return DefWindowProc(hwnd, msg, wParam, lParam);

        switch (msg) {
        case WM_LBUTTONDOWN:
            overlay->isSelecting = true;
            overlay->startPoint.x = GET_X_LPARAM(lParam);
            overlay->startPoint.y = GET_Y_LPARAM(lParam);
            overlay->endPoint = overlay->startPoint;
            break;

        case WM_RBUTTONDOWN:
            overlay->Hide();
            break;

        case WM_MOUSEMOVE:
            if (overlay->isSelecting) {
                overlay->endPoint.x = GET_X_LPARAM(lParam);
                overlay->endPoint.y = GET_Y_LPARAM(lParam);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case WM_LBUTTONUP:
            if (overlay->isSelecting) {
                overlay->isSelecting = false;

                // 计算选择区域
                auto [minX, maxX] = std::minmax(overlay->startPoint.x, overlay->endPoint.x);
                auto [minY, maxY] = std::minmax(overlay->startPoint.y, overlay->endPoint.y);

                overlay->selectedRect = RECT{
                    .left = minX,
                    .top = minY,
                    .right = maxX,
                    .bottom = maxY
                };

                overlay->Hide(true);
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                overlay->Hide();
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // 绘制半透明背景
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            auto brush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &clientRect, brush);
            DeleteObject(brush);

            // 绘制选择框
            if (overlay->isSelecting) {
                auto pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                auto oldPen = SelectObject(hdc, pen);

                auto [minX, maxX] = std::minmax(overlay->startPoint.x, overlay->endPoint.x);
                auto [minY, maxY] = std::minmax(overlay->startPoint.y, overlay->endPoint.y);

                Rectangle(hdc, minX, minY, maxX, maxY);

                SelectObject(hdc, oldPen);
                DeleteObject(pen);
            }

            EndPaint(hwnd, &ps);
            break;
        }

        case WM_TIMER:
            if (wParam == 1 && overlay->fadingIn) {
                overlay->alpha = static_cast<BYTE>(std::min<int>(overlay->alpha + 16, 128));
                SetLayeredWindowAttributes(hwnd, 0, overlay->alpha, LWA_ALPHA);
                if (overlay->alpha >= 128) {
                    KillTimer(hwnd, 1);
                    overlay->fadingIn = false;
                    SetCapture(hwnd);
                }
            }
            else if (wParam == 2 && overlay->fadingOut) {
                if (overlay->alpha <= 16) {
                    overlay->alpha = 0;
                    SetLayeredWindowAttributes(hwnd, 0, overlay->alpha, LWA_ALPHA);
                    KillTimer(hwnd, 2);
                    overlay->fadingOut = false;
                    ShowWindow(hwnd, SW_HIDE);
                    ReleaseCapture();
                    auto style2 = GetWindowLong(hwnd, GWL_EXSTYLE);
                    SetWindowLong(hwnd, GWL_EXSTYLE, style2 | WS_EX_TRANSPARENT);
                    overlay->isSelecting = false;
                    if (overlay->notifyHide && overlay->messageWnd)
                        PostMessage(overlay->messageWnd, WM_USER + 100, 0, 0);
                    overlay->notifyHide = false;
                } else {
                    overlay->alpha = static_cast<BYTE>(overlay->alpha - 16);
                    SetLayeredWindowAttributes(hwnd, 0, overlay->alpha, LWA_ALPHA);
                }
            }
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        return 0;
    }
};

// 主应用程序类
class ScreenshotApp {
private:
    HWND hwnd = nullptr;
    NOTIFYICONDATA nid{};
    std::unique_ptr<HDRScreenCapture> capture;
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

        // 初始化HDR截图
        capture = std::make_unique<HDRScreenCapture>();
        if (!capture->Initialize()) {
            MessageBox(nullptr, L"Failed to initialize HDR capture", L"Error", MB_OK);
            return false;
        }

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
            }
        }
    }

    void SaveConfig() {
        std::ofstream file("config.ini");
        file << "; HDR Screenshot Tool Configuration\n"
            << std::format("RegionHotkey={}\n", config.regionHotkey)
            << std::format("FullscreenHotkey={}\n", config.fullscreenHotkey)
            << std::format("SavePath={}\n", config.savePath)
            << std::format("AutoStart={}\n", config.autoStart ? "true" : "false")
            << std::format("SaveToFile={}\n", config.saveToFile ? "true" : "false");
    }

    void CreateTrayIcon() {
        nid = NOTIFYICONDATA{
            .cbSize = sizeof(nid),
            .hWnd = hwnd,
            .uID = IDI_TRAY_ICON,
            .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP,
            .uCallbackMessage = WM_TRAY_ICON,
            .hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_TRAY_ICON))
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

    void ToggleAutoStart() {
        config.autoStart = !config.autoStart;
        SaveConfig();

        constexpr auto keyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
        constexpr auto valueName = L"HDRScreenshotTool";

        HKEY hkey;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &hkey) == ERROR_SUCCESS) {
            auto keyGuard = [hkey](void*) { RegCloseKey(hkey); };
            std::unique_ptr<void, decltype(keyGuard)> guard(reinterpret_cast<void*>(1), keyGuard);

            if (config.autoStart) {
                wchar_t exePath[MAX_PATH];
                GetModuleFileName(nullptr, exePath, MAX_PATH);
                RegSetValueEx(hkey, valueName, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(exePath),
                    (wcslen(exePath) + 1) * sizeof(wchar_t));
            }
            else {
                RegDeleteValue(hkey, valueName);
            }
        }
    }

    void ToggleSaveToFile() {
        config.saveToFile = !config.saveToFile;
        SaveConfig();
    }

    void TakeRegionScreenshot() {
        overlay->Show();
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

        if (capture->CaptureFullscreen(filename.value_or(""))) {
            if (config.saveToFile) {
                ShowNotification(L"全屏截图已保存", std::wstring(filename->begin(), filename->end()));
            } else {
                ShowNotification(L"全屏截图已复制到剪贴板");
            }
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

                filename = std::format("{}/region_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.png",
                    config.savePath, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
            }

            if (capture->CaptureRegion(rect.left, rect.top, width, height, filename.value_or(""))) {
                if (config.saveToFile) {
                    ShowNotification(L"区域截图已保存", std::wstring(filename->begin(), filename->end()));
                } else {
                    ShowNotification(L"区域截图已复制到剪贴板");
                }
            }
        }
    }

    void ShowNotification(const std::wstring& message,
        const std::optional<std::wstring>& path = std::nullopt) {
        nid.uFlags = NIF_INFO;
        std::wstring info = message;
        if (path) info += L"\n" + *path;
        wcsncpy_s(nid.szInfo, info.c_str(), _TRUNCATE);
        wcscpy_s(nid.szInfoTitle, L"HDR Screenshot Tool");
        Shell_NotifyIcon(NIM_MODIFY, &nid);
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

// 程序入口点
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 防止多实例运行
    auto hMutex = CreateMutex(nullptr, TRUE, L"HDRScreenshotTool_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    auto mutexGuard = [hMutex](void*) { CloseHandle(hMutex); };
    std::unique_ptr<void, decltype(mutexGuard)> guard(reinterpret_cast<void*>(1), mutexGuard);

    // 初始化GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    auto gdiplusGuard = [gdiplusToken](void*) { GdiplusShutdown(gdiplusToken); };
    std::unique_ptr<void, decltype(gdiplusGuard)> gdiplusCleanup(reinterpret_cast<void*>(1), gdiplusGuard);

    // 初始化COM
    CoInitialize(nullptr);
    auto comGuard = [](void*) { CoUninitialize(); };
    std::unique_ptr<void, decltype(comGuard)> comCleanup(reinterpret_cast<void*>(1), comGuard);

    // 创建并运行应用程序
    auto app = std::make_unique<ScreenshotApp>();
    if (app->Initialize()) {
        app->Run();
    }

    app->Cleanup();

    return 0;
}