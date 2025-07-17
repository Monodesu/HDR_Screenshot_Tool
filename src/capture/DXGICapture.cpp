#include "DXGICapture.hpp"
#include "../util/Logger.hpp"

namespace screenshot_tool {

    bool DXGICapture::Initialize() {
        initialized_ = initDxgiObjects();
        if (initialized_) detectHDR();
        return initialized_;
    }

    bool DXGICapture::Reinitialize() {
        dupl_.Reset(); context_.Reset(); device_.Reset();
        initialized_ = false; hdrEnabled_ = false; hdrMeta_ = {};
        return Initialize();
    }

    bool DXGICapture::initDxgiObjects() {
        // TODO: ��ʵ DXGI ö�� + DuplicateOutput1; ��ǰ stub
        virtualRect_ = { 0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN) };
        return true;
    }

    void DXGICapture::detectHDR() {
        // TODO: ��ʵ HDR ��⣻��ǰ���� SDR
        hdrEnabled_ = false;
    }

    CaptureResult DXGICapture::CaptureRegion(int x, int y, int w, int h, DXGI_FORMAT& fmt, ImageBuffer& out) {
        // TODO: ���� GPU ���ƣ���ǰ stub ����� buffer
        fmt = DXGI_FORMAT_B8G8R8A8_UNORM; // 设置输出格式
        out.format = PixelFormat::BGRA8; out.width = w; out.height = h; out.stride = w * 4; out.data.resize(out.stride * h);
        std::fill(out.data.begin(), out.data.end(), 0x80); // ��ɫ
        return CaptureResult::Success;
    }

} // namespace screenshot_tool