#pragma once
#include "CaptureCommon.hpp"
#include "../image/ImageBuffer.hpp"
#include "../config/Config.hpp"
#include "../platform/WinHeaders.hpp"
#include <vector>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace screenshot_tool {

    struct MonitorInfo {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> dupl;
        RECT desktopRect{};
        UINT width = 0;
        UINT height = 0;
        DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
    };

    class DXGICapture {
    public:
        DXGICapture() = default;
        bool Initialize();
        bool Reinitialize();

        bool IsInitialized() const { return initialized_; }
        bool IsHDREnabled() const { return hdrEnabled_; }
        HDRMetadata GetHDRMetadata() const { return hdrMeta_; }
        RECT GetVirtualRect() const { return virtualRect_; }
        const std::vector<MonitorInfo>& GetMonitors() const { return monitors_; }

        // 底层抓屏获取原始格式 (上层再根据 HDR 判断 & 转换)
        CaptureResult CaptureRegion(int x, int y, int w, int h, DXGI_FORMAT& fmt, ImageBuffer& out);

    private:
        bool initialized_ = false;
        bool hdrEnabled_ = false;
        HDRMetadata hdrMeta_{};
        RECT virtualRect_{};
        std::vector<MonitorInfo> monitors_;

        bool initDxgiObjects();
        void detectHDR();
        bool InitMonitor(MonitorInfo& info);
    };

} // namespace screenshot_tool