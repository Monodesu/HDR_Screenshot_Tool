#pragma once
#include "CaptureCommon.hpp"
#include "../image/ImageBuffer.hpp"
#include "../config/Config.hpp"
#include "../platform/WinHeaders.hpp"
#include <vector>
#include <dxgi.h>

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

        // ����ץ����ȡԭʼ��ʽ (���������� HDR �ж� & ת��)
        CaptureResult CaptureRegion(int x, int y, int w, int h, DXGI_FORMAT& fmt, ImageBuffer& out);

    private:
        bool initialized_ = false;
        bool hdrEnabled_ = false;
        HDRMetadata hdrMeta_{};
        RECT virtualRect_{};

        // DXGI ���󼯺� (��ֻץ��һ��ʾ����������չ)
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> dupl_;
        Microsoft::WRL::ComPtr<ID3D11Device>          device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>   context_;

        bool initDxgiObjects();
        void detectHDR();
    };

} // namespace screenshot_tool