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
        // TODO: 真实 DXGI 枚举 + DuplicateOutput1; 当前 stub
        virtualRect_ = { 0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN) };
        return true;
    }

    void DXGICapture::detectHDR() {
        // TODO: 真实 HDR 检测；当前假设 SDR
        hdrEnabled_ = false;
    }

    CaptureResult DXGICapture::CaptureRegion(int x, int y, int w, int h, ImageBuffer& out) {
        // TODO: 真正 GPU 复制；当前 stub 输出空 buffer
        out.format = PixelFormat::BGRA8; out.width = w; out.height = h; out.stride = w * 4; out.data.resize(out.stride * h);
        std::fill(out.data.begin(), out.data.end(), 0x80); // 灰色
        return CaptureResult::Success;
    }

} // namespace screenshot_tool