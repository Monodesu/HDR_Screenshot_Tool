#pragma once
#include "CaptureCommon.hpp"
#include "../image/ImageBuffer.hpp"
#include "../config/Config.hpp"
#include "../platform/WinHeaders.hpp"
#include <vector>

namespace screenshot_tool {

    class DXGICapture {
    public:
        DXGICapture() = default;
        bool Initialize();
        bool Reinitialize();

        bool IsInitialized() const { return initialized_; }
        bool IsHDREnabled() const { return hdrEnabled_; }
        HDRMetadata GetHDRMetadata() const { return hdrMeta_; }
        RECT GetVirtualRect() const { return virtualRect_; }

        // 区域抓屏：取原始格式 (后续将补充 HDR 判定 & 转换)
        CaptureResult CaptureRegion(int x, int y, int w, int h, ImageBuffer& out);

    private:
        bool initialized_ = false;
        bool hdrEnabled_ = false;
        HDRMetadata hdrMeta_{};
        RECT virtualRect_{};

        // DXGI 对象集合 (暂只抓第一显示器；后续扩展)
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> dupl_;
        Microsoft::WRL::ComPtr<ID3D11Device>          device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>   context_;

        bool initDxgiObjects();
        void detectHDR();
    };

} // namespace screenshot_tool