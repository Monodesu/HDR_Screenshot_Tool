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
    float hdrExposure = 0.5f;  // 降低默认曝光
    bool debugMode = false; // 调试模式
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
    bool isHDREnabled = false;
    const Config* config = nullptr; // 添加配置引用


public:
    void SetConfig(const Config* cfg) { config = cfg; }
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

        // 检测HDR状态
        DetectHDRStatus();

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
    void DetectHDRStatus() {
        // 检测显示器是否支持HDR并已启用
        DXGI_OUTPUT_DESC1 outputDesc1;
        isHDREnabled = false; // 默认为false

        if (output6) {
            ComPtr<IDXGIOutput6> output6Temp;
            if (SUCCEEDED(output6.As(&output6Temp))) {
                if (SUCCEEDED(output6Temp->GetDesc1(&outputDesc1))) {
                    // 输出调试信息
                    std::ofstream debug("hdr_detection.txt");
                    debug << "Monitor Info:" << std::endl;
                    debug << "ColorSpace: " << static_cast<int>(outputDesc1.ColorSpace) << std::endl;
                    debug << "MaxLuminance: " << outputDesc1.MaxLuminance << std::endl;
                    debug << "MinLuminance: " << outputDesc1.MinLuminance << std::endl;
                    debug << "MaxFullFrameLuminance: " << outputDesc1.MaxFullFrameLuminance << std::endl;

                    // Windows HDR模式的颜色空间检查
                    // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12
                    // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 = 1
                    isHDREnabled = (outputDesc1.ColorSpace == 12) || // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                        (outputDesc1.ColorSpace == 1);    // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709

                    debug << "HDR detection based on ColorSpace: " << (isHDREnabled ? "Yes" : "No") << std::endl;

                    // 额外检查：如果MaxLuminance > 80，也认为是HDR
                    if (!isHDREnabled && outputDesc1.MaxLuminance > 80.0f) {
                        isHDREnabled = true;
                        debug << "HDR detection based on MaxLuminance: Yes" << std::endl;
                    }

                    // 强制检测：暂时假设任何非默认设置都是HDR
                    if (!isHDREnabled) {
                        isHDREnabled = true; // 强制启用HDR处理进行测试
                        debug << "Force HDR processing to be enabled for testing" << std::endl;
                    }

                    debug << "HDR Status: " << (isHDREnabled ? "Enabled" : "Disabled") << std::endl;
                    debug.close();
                }
            }
        }

        // 如果无法获取信息，默认启用HDR处理
        if (!isHDREnabled) {
            isHDREnabled = true;
            std::ofstream debug("hdr_detection.txt", std::ios::app);
            debug << "Unable to obtain display information, HDR processing is enabled by default" << std::endl;
            debug.close();
        }
    }


    bool ProcessAndSave(uint8_t* data, int width, int height, int pitch,
        DXGI_FORMAT format, const std::string& filename) {

        std::vector<uint8_t> rgbBuffer(width * height * 3);

        // 添加调试信息
        static bool debugOnce = true;
        if (debugOnce) {
            std::ofstream debug("format_debug.txt");
            debug << "DXGI Format: " << static_cast<int>(format) << std::endl;
            debug << "HDR Detected: " << (isHDREnabled ? "Yes" : "No") << std::endl;
            debug << "Handling branches: ";
            debugOnce = false;

            // 根据格式和HDR状态处理数据
            switch (format) {
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                debug << "R16G16B16A16_FLOAT";
                if (isHDREnabled) {
                    debug << " -> HDR Processing" << std::endl;
                    ProcessHDR16Float(data, rgbBuffer.data(), width, height, pitch);
                }
                else {
                    debug << " -> SDR Processing" << std::endl;
                    ProcessSDR16Float(data, rgbBuffer.data(), width, height, pitch);
                }
                break;
            case DXGI_FORMAT_R10G10B10A2_UNORM:
                debug << "R10G10B10A2_UNORM";
                if (isHDREnabled) {
                    debug << " -> HDR Processing" << std::endl;
                    ProcessHDR10(data, rgbBuffer.data(), width, height, pitch);
                }
                else {
                    debug << " -> SDR Processing" << std::endl;
                    ProcessSDR10(data, rgbBuffer.data(), width, height, pitch);
                }
                break;
            default:
                debug << "Other format(" << static_cast<int>(format) << ") -> SDR Processing" << std::endl;
                ProcessSDR(data, rgbBuffer.data(), width, height, pitch);
                break;
            }
            debug.close();
        }
        else {
            // 正常处理分支
            switch (format) {
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                if (isHDREnabled) {
                    ProcessHDR16Float(data, rgbBuffer.data(), width, height, pitch);
                }
                else {
                    ProcessSDR16Float(data, rgbBuffer.data(), width, height, pitch);
                }
                break;
            case DXGI_FORMAT_R10G10B10A2_UNORM:
                if (isHDREnabled) {
                    ProcessHDR10(data, rgbBuffer.data(), width, height, pitch);
                }
                else {
                    ProcessSDR10(data, rgbBuffer.data(), width, height, pitch);
                }
                break;
            default:
                ProcessSDR(data, rgbBuffer.data(), width, height, pitch);
                break;
            }
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
        // 添加调试输出
        static bool debugOnce = true;
        if (debugOnce) {
            std::ofstream debug("process_debug.txt", std::ios::app);
            debug << "ProcessHDR16Float smart mode" << std::endl;
            debug << "HDR exposure value: " << GetHDRExposure() << std::endl;
            debugOnce = false;
        }

        // 调试模式：输出原始数据
        if (GetDebugMode()) {
            static bool debugRawOnce = true;
            if (debugRawOnce) {
                std::ofstream debug("hdr_raw_data.txt");
                debug << "HDR raw data analysis (first 100 pixels):\n";
                auto* srcRow = reinterpret_cast<uint16_t*>(src);
                for (int i = 0; i < 100 && i < width; ++i) {
                    float r = HalfToFloat(srcRow[i * 4 + 0]);
                    float g = HalfToFloat(srcRow[i * 4 + 1]);
                    float b = HalfToFloat(srcRow[i * 4 + 2]);
                    debug << std::format("Pixel{}: R={:.6f} G={:.6f} B={:.6f}\n", i, r, g, b);
                }
                debugRawOnce = false;
            }
        }

        for (int y : std::views::iota(0, height)) {
            auto* srcRow = reinterpret_cast<uint16_t*>(src + y * pitch);
            auto* dstRow = dst + y * width * 3;

            for (int x : std::views::iota(0, width)) {
                float r = HalfToFloat(srcRow[x * 4 + 0]);
                float g = HalfToFloat(srcRow[x * 4 + 1]);
                float b = HalfToFloat(srcRow[x * 4 + 2]);

                // 如果不是调试模式，强制暗化
                if (!GetDebugMode()) {
                    r *= 0.1f; // 强制暗化到10%
                    g *= 0.1f;
                    b *= 0.1f;
                }

                // 智能处理：根据当前像素亮度调整曝光并避免过曝
                float maxChannel = std::max({ r, g, b });
                float scale = GetHDRExposure();
                if (maxChannel > 1.0f) {
                    scale = std::min(scale, 1.0f / maxChannel);
                }
                r = std::clamp(r * scale, 0.0f, 1.0f);
                g = std::clamp(g * scale, 0.0f, 1.0f);
                b = std::clamp(b * scale, 0.0f, 1.0f);

                // 跳过伽马校正看看是否有影响
                // r = LinearToSRGB(r);
                // g = LinearToSRGB(g);
                // b = LinearToSRGB(b);

                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
    }

    // 添加获取配置的方法
    float GetHDRExposure() const {
        return config ? config->hdrExposure : 0.5f;
    }
    bool GetDebugMode() const {
        return config ? config->debugMode : false;
    }


    void ProcessHDR10(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
        // HDR10格式使用PQ编码和Rec.2020色域

        for (int y : std::views::iota(0, height)) {
            auto* srcRow = reinterpret_cast<uint32_t*>(src + y * pitch);
            auto* dstRow = dst + y * width * 3;

            for (int x : std::views::iota(0, width)) {
                uint32_t pixel = srcRow[x];
                uint32_t r10 = (pixel >> 20) & 0x3FF;
                uint32_t g10 = (pixel >> 10) & 0x3FF;
                uint32_t b10 = pixel & 0x3FF;

                // PQ解码到线性光域（nits）
                float r = PQToLinear(static_cast<float>(r10) / 1023.0f);
                float g = PQToLinear(static_cast<float>(g10) / 1023.0f);
                float b = PQToLinear(static_cast<float>(b10) / 1023.0f);

                // 归一化到合理范围（PQ解码后是0-10000 nits）
                constexpr float maxNits = 1000.0f; // 大多数内容的峰值亮度
                r = std::clamp(r / maxNits, 0.0f, 1.0f);
                g = std::clamp(g / maxNits, 0.0f, 1.0f);
                b = std::clamp(b / maxNits, 0.0f, 1.0f);

                // Rec.2020 到 sRGB 色域转换
                Rec2020ToSRGB(r, g, b);

                // 简单色调映射
                r = SimpleToneMapping(r);
                g = SimpleToneMapping(g);
                b = SimpleToneMapping(b);

                // sRGB伽马校正
                r = LinearToSRGB(std::clamp(r, 0.0f, 1.0f));
                g = LinearToSRGB(std::clamp(g, 0.0f, 1.0f));
                b = LinearToSRGB(std::clamp(b, 0.0f, 1.0f));

                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
    }

    void ProcessSDR16Float(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
        // 添加调试输出
        static bool debugOnce = true;
        if (debugOnce) {
            std::ofstream debug("process_debug.txt", std::ios::app);
            debug << "调用了 ProcessSDR16Float" << std::endl;
            debugOnce = false;
        }

        for (int y : std::views::iota(0, height)) {
            auto* srcRow = reinterpret_cast<uint16_t*>(src + y * pitch);
            auto* dstRow = dst + y * width * 3;

            for (int x : std::views::iota(0, width)) {
                float r = HalfToFloat(srcRow[x * 4 + 0]);
                float g = HalfToFloat(srcRow[x * 4 + 1]);
                float b = HalfToFloat(srcRow[x * 4 + 2]);

                // SDR模式下直接钳制到0-1
                r = std::clamp(r, 0.0f, 1.0f);
                g = std::clamp(g, 0.0f, 1.0f);
                b = std::clamp(b, 0.0f, 1.0f);

                // 应用伽马校正
                r = LinearToSRGB(r);
                g = LinearToSRGB(g);
                b = LinearToSRGB(b);

                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
    }

    void ProcessSDR10(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
        for (int y : std::views::iota(0, height)) {
            auto* srcRow = reinterpret_cast<uint32_t*>(src + y * pitch);
            auto* dstRow = dst + y * width * 3;

            for (int x : std::views::iota(0, width)) {
                uint32_t pixel = srcRow[x];
                uint32_t r10 = (pixel >> 20) & 0x3FF;
                uint32_t g10 = (pixel >> 10) & 0x3FF;
                uint32_t b10 = pixel & 0x3FF;

                // SDR模式下简单缩放
                float r = static_cast<float>(r10) / 1023.0f;
                float g = static_cast<float>(g10) / 1023.0f;
                float b = static_cast<float>(b10) / 1023.0f;

                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
    }

    void ProcessSDR(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
        // 添加调试输出
        static bool debugOnce = true;
        if (debugOnce) {
            std::ofstream debug("process_debug.txt", std::ios::app);
            debug << "ProcessSDR (BGRA) called." << std::endl;
            debugOnce = false;
        }

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

    static constexpr float SimpleToneMapping(float x) noexcept {
        // 非常简单的色调映射，几乎不压缩
        return x / (1.0f + x * 0.1f);
    }

    static constexpr float ImprovedToneMapping(float x) noexcept {
        // Reinhard扩展色调映射，比ACES更温和
        constexpr float whitePoint = 1.0f;
        constexpr float exposure = 1.0f;

        x *= exposure;
        return (x * (1.0f + x / (whitePoint * whitePoint))) / (1.0f + x);
    }

    static float LinearToSRGB(float linear) noexcept {
        if (linear <= 0.0031308f) {
            return 12.92f * linear;
        }
        else {
            return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        }
    }

    static void Rec2020ToSRGB(float& r, float& g, float& b) noexcept {
        // 使用Bradford色度适应的精确转换矩阵
        float r2 = 1.7166511f * r - 0.3556708f * g - 0.2533663f * b;
        float g2 = -0.6666844f * r + 1.6164812f * g + 0.0157685f * b;
        float b2 = 0.0176399f * r - 0.0427706f * g + 0.9421031f * b;

        r = std::clamp(r2, 0.0f, 1.0f);
        g = std::clamp(g2, 0.0f, 1.0f);
        b = std::clamp(b2, 0.0f, 1.0f);
    }

    static float PQToLinear(float pq) noexcept {
        // ST2084 EOTF - 更精确的实现
        constexpr float m1 = 2610.0f / 16384.0f;
        constexpr float m2 = 2523.0f / 4096.0f * 128.0f;
        constexpr float c1 = 3424.0f / 4096.0f;
        constexpr float c2 = 2413.0f / 4096.0f * 32.0f;
        constexpr float c3 = 2392.0f / 4096.0f * 32.0f;

        pq = std::clamp(pq, 0.0f, 1.0f);

        if (pq == 0.0f) return 0.0f;

        float p = std::pow(pq, 1.0f / m2);
        float num = std::max(p - c1, 0.0f);
        float den = c2 - c3 * p;

        if (den <= 0.0f) return 0.0f;

        return std::pow(num / den, 1.0f / m1) * 10000.0f;
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
        if (!OpenClipboard(nullptr)) return false;

        auto clipboardGuard = [](void*) { CloseClipboard(); };
        std::unique_ptr<void, decltype(clipboardGuard)> guard(reinterpret_cast<void*>(1), clipboardGuard);

        EmptyClipboard();

        // 计算位图数据大小
        int rowSize = ((width * 24 + 31) / 32) * 4; // 4字节对齐
        int imageSize = rowSize * height;
        int totalSize = sizeof(BITMAPINFOHEADER) + imageSize;

        auto hDib = GlobalAlloc(GMEM_MOVEABLE, totalSize);
        if (!hDib) return false;

        auto* bih = static_cast<BITMAPINFOHEADER*>(GlobalLock(hDib));
        if (!bih) {
            GlobalFree(hDib);
            return false;
        }

        // 填充位图信息头
        *bih = BITMAPINFOHEADER{
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = width,
            .biHeight = height, // 正数表示从下到上
            .biPlanes = 1,
            .biBitCount = 24,
            .biCompression = BI_RGB,
            .biSizeImage = static_cast<DWORD>(imageSize)
        };

        // 复制图像数据（需要垂直翻转，RGB转BGR）
        auto* dibData = reinterpret_cast<uint8_t*>(bih + 1);
        for (int y = 0; y < height; ++y) {
            auto* dstRow = dibData + (height - 1 - y) * rowSize;
            auto* srcRow = data.data() + y * width * 3;
            for (int x = 0; x < width; ++x) {
                dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
                dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
                dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
            }
        }

        GlobalUnlock(hDib);

        if (SetClipboardData(CF_DIB, hDib)) {
            return true; // 成功时不释放内存，系统会管理
        }
        else {
            GlobalFree(hDib);
            return false;
        }
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

// 改进的区域选择覆盖窗口
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
    HDC memDC = nullptr;
    HBITMAP memBitmap = nullptr;
    HBITMAP oldBitmap = nullptr;

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

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        hwnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
            L"SelectionOverlay", L"",
            WS_POPUP,
            0, 0, screenWidth, screenHeight,
            nullptr, nullptr, GetModuleHandle(nullptr), this);

        if (!hwnd) return false;

        // 创建内存DC和位图以避免闪烁
        HDC hdc = GetDC(hwnd);
        memDC = CreateCompatibleDC(hdc);
        memBitmap = CreateCompatibleBitmap(hdc, screenWidth, screenHeight);
        oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));
        ReleaseDC(hwnd, hdc);

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
        if (memDC) {
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
        }
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
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
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
                ReleaseCapture();

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

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            // 使用内存DC绘制以避免闪烁
            // 清除背景
            auto brush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(overlay->memDC, &clientRect, brush);
            DeleteObject(brush);

            // 绘制选择框
            if (overlay->isSelecting) {
                auto pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                auto oldPen = SelectObject(overlay->memDC, pen);

                auto [minX, maxX] = std::minmax(overlay->startPoint.x, overlay->endPoint.x);
                auto [minY, maxY] = std::minmax(overlay->startPoint.y, overlay->endPoint.y);

                // 绘制选择框
                SetBkMode(overlay->memDC, TRANSPARENT);
                Rectangle(overlay->memDC, minX, minY, maxX, maxY);

                // 绘制尺寸信息
                auto oldTextColor = SetTextColor(overlay->memDC, RGB(255, 255, 255));
                auto font = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                auto oldFont = SelectObject(overlay->memDC, font);

                auto width = abs(maxX - minX);
                auto height = abs(maxY - minY);
                auto text = std::format(L"{}×{}", width, height);

                TextOut(overlay->memDC, minX, minY - 25, text.c_str(), static_cast<int>(text.length()));

                SelectObject(overlay->memDC, oldFont);
                DeleteObject(font);
                SetTextColor(overlay->memDC, oldTextColor);
                SelectObject(overlay->memDC, oldPen);
                DeleteObject(pen);
            }

            // 将内存DC内容复制到窗口DC
            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, overlay->memDC, 0, 0, SRCCOPY);

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
                }
            }
            else if (wParam == 2 && overlay->fadingOut) {
                if (overlay->alpha <= 16) {
                    overlay->alpha = 0;
                    SetLayeredWindowAttributes(hwnd, 0, overlay->alpha, LWA_ALPHA);
                    KillTimer(hwnd, 2);
                    overlay->fadingOut = false;
                    ShowWindow(hwnd, SW_HIDE);
                    auto style2 = GetWindowLong(hwnd, GWL_EXSTYLE);
                    SetWindowLong(hwnd, GWL_EXSTYLE, style2 | WS_EX_TRANSPARENT);
                    overlay->isSelecting = false;
                    if (overlay->notifyHide && overlay->messageWnd)
                        PostMessage(overlay->messageWnd, WM_USER + 100, 0, 0);
                    overlay->notifyHide = false;
                }
                else {
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
        capture->SetConfig(&config); // 传递配置
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
                else if (key == "HDRExposure") config.hdrExposure = std::stof(value);
                
                else if (key == "DebugMode") config.debugMode = (value == "true");
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
            << "\n; HDR Processing Settings\n"
            << "; HDRExposure: 0.1-2.0, lower=darker (try 0.3 if still too bright)\n"
            << std::format("HDRExposure={}\n", config.hdrExposure)
            << "; DebugMode: outputs raw HDR values to debug file\n"
            << std::format("DebugMode={}\n", config.debugMode ? "true" : "false");
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

                filename = std::format("{}/region_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}.png",
                    config.savePath, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
            }

            if (capture->CaptureRegion(rect.left, rect.top, width, height, filename.value_or(""))) {
                if (config.saveToFile && filename) {
                    ShowNotification(L"区域截图已保存", std::wstring(filename->begin(), filename->end()));
                }
                else {
                    ShowNotification(L"区域截图已复制到剪贴板");
                }
            }
            else {
                ShowNotification(L"截图失败");
            }
        }
    }

    void ShowNotification(const std::wstring& message,
        const std::optional<std::wstring>& path = std::nullopt) {

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