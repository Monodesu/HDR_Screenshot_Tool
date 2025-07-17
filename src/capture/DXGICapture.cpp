#include "DXGICapture.hpp"
#include "../util/Logger.hpp"
#include "../platform/WinHeaders.hpp"
#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace screenshot_tool {


    bool DXGICapture::Initialize() {
        monitors_.clear();
        virtualRect_ = {};
        initialized_ = initDxgiObjects();
        if (initialized_) detectHDR();
        return initialized_;
    }

    bool DXGICapture::Reinitialize() {
        // 清理所有监视器的资源
        for (auto& monitor : monitors_) {
            monitor.dupl.Reset();
            monitor.context.Reset();
            monitor.device.Reset();
        }
        monitors_.clear();
        initialized_ = false;
        hdrEnabled_ = false;
        hdrMeta_ = {};
        return Initialize();
    }

    bool DXGICapture::initDxgiObjects() {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            Logger::Error(L"Failed to create DXGI factory");
            return false;
        }

        UINT adapterIndex = 0;
        RECT virtualRect{ INT_MAX, INT_MAX, INT_MIN, INT_MIN };
        
        while (true) {
            ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapters1(adapterIndex, &adapter))) break;

            UINT outputIndex = 0;
            while (true) {
                ComPtr<IDXGIOutput> output;
                if (FAILED(adapter->EnumOutputs(outputIndex, &output))) break;

                MonitorInfo monitor{};
                monitor.adapter = adapter;
                if (FAILED(output.As(&monitor.output6))) { 
                    ++outputIndex; 
                    continue; 
                }

                DXGI_OUTPUT_DESC desc;
                monitor.output6->GetDesc(&desc);
                monitor.desktopRect = desc.DesktopCoordinates;

                virtualRect.left = std::min(virtualRect.left, desc.DesktopCoordinates.left);
                virtualRect.top = std::min(virtualRect.top, desc.DesktopCoordinates.top);
                virtualRect.right = std::max(virtualRect.right, desc.DesktopCoordinates.right);
                virtualRect.bottom = std::max(virtualRect.bottom, desc.DesktopCoordinates.bottom);

                if (InitMonitor(monitor)) {
                    monitors_.push_back(std::move(monitor));
                }

                ++outputIndex;
            }
            ++adapterIndex;
        }

        if (monitors_.empty()) {
            Logger::Error(L"No valid monitors found");
            return false;
        }

        virtualRect_ = virtualRect;
        Logger::Info(L"DXGI initialized with {} monitors", monitors_.size());
        return true;
    }

    bool DXGICapture::InitMonitor(MonitorInfo& info) {
        // 清理之前的资源
        info.dupl.Reset();
        info.context.Reset();
        info.device.Reset();

        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(
            info.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &info.device, &fl, &info.context);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create D3D11 device, HRESULT: 0x{:x}", static_cast<unsigned>(hr));
            return false;
        }

        // 尝试使用更好的格式
        DXGI_FORMAT fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hr = info.output6->DuplicateOutput1(info.device.Get(), 0, 1, &fmt, &info.dupl);
        if (FAILED(hr)) {
            // 回退到标准输出复制
            hr = info.output6->DuplicateOutput(info.device.Get(), &info.dupl);
            if (FAILED(hr)) {
                Logger::Error(L"Failed to duplicate output, HRESULT: 0x{:x}", static_cast<unsigned>(hr));
                return false;
            }
        }

        DXGI_OUTDUPL_DESC dd{};
        info.dupl->GetDesc(&dd);
        info.rotation = dd.Rotation;
        info.width = dd.ModeDesc.Width;
        info.height = dd.ModeDesc.Height;

        return true;
    }

    void DXGICapture::detectHDR() {
        hdrEnabled_ = false;
        
        if (!monitors_.empty()) {
            DXGI_OUTPUT_DESC1 outputDesc1;
            if (SUCCEEDED(monitors_.front().output6->GetDesc1(&outputDesc1))) {
                // 检测 HDR10/PQ 色彩空间
                hdrEnabled_ = (outputDesc1.ColorSpace == 12); // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                
                // 额外检查：如果 MaxLuminance > 400nits，可能是 HDR 显示器
                if (!hdrEnabled_ && outputDesc1.MaxLuminance > 400.0f) {
                    hdrEnabled_ = true;
                }
                
                hdrMeta_.maxLuminance = outputDesc1.MaxLuminance;
                hdrMeta_.minLuminance = outputDesc1.MinLuminance;
                hdrMeta_.maxContentLightLevel = outputDesc1.MaxFullFrameLuminance;
                
                Logger::Info(L"HDR detection: {} (ColorSpace: {}, MaxLuminance: {})", 
                    hdrEnabled_ ? L"Yes" : L"No", 
                    static_cast<int>(outputDesc1.ColorSpace), 
                    outputDesc1.MaxLuminance);
            }
        }
    }

    CaptureResult DXGICapture::CaptureRegion(int x, int y, int w, int h, DXGI_FORMAT& fmt, ImageBuffer& out) {
        RECT regionRect{ x, y, x + w, y + h };
        
        fmt = DXGI_FORMAT_UNKNOWN;
        UINT bpp = 0;
        out.data.clear();
        bool gotFrame = false;
        bool allSuccess = true;

        for (auto& m : monitors_) {
            RECT inter{};
            if (!IntersectRect(&inter, &regionRect, &m.desktopRect)) continue;

            ComPtr<IDXGIResource> resource;
            DXGI_OUTDUPL_FRAME_INFO frameInfo;
            HRESULT hr = m.dupl->AcquireNextFrame(100, &frameInfo, &resource);
            if (FAILED(hr)) {
                if (hr == DXGI_ERROR_ACCESS_LOST || 
                    hr == DXGI_ERROR_DEVICE_REMOVED ||
                    hr == DXGI_ERROR_SESSION_DISCONNECTED) {
                    allSuccess = false;
                    gotFrame = false;
                    continue;
                } else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                    allSuccess = false;
                    continue;
                } else {
                    allSuccess = false; 
                    continue; 
                }
            }
            gotFrame = true;

            auto cleanup = [&](void*) { m.dupl->ReleaseFrame(); };
            std::unique_ptr<void, decltype(cleanup)> frameGuard(reinterpret_cast<void*>(1), cleanup);

            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resource.As(&texture))) continue;

            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (fmt == DXGI_FORMAT_UNKNOWN) {
                fmt = desc.Format;
                bpp = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
                out.format = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? PixelFormat::RGBA_F16 : PixelFormat::BGRA8;
                out.width = w;
                out.height = h;
                out.stride = w * bpp;
                out.data.resize(out.stride * h);
            }

            D3D11_TEXTURE2D_DESC stagingDesc = desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.BindFlags = 0;
            stagingDesc.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(m.device->CreateTexture2D(&stagingDesc, nullptr, &staging))) continue;
            m.context->CopyResource(staging.Get(), texture.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;

            auto unmap = [&](void*) { m.context->Unmap(staging.Get(), 0); };
            std::unique_ptr<void, decltype(unmap)> mapGuard(reinterpret_cast<void*>(1), unmap);

            int destX = inter.left - x;
            int destY = inter.top - y;
            int rw = inter.right - inter.left;
            int rh = inter.bottom - inter.top;

            int rx0 = inter.left - m.desktopRect.left;
            int ry0 = inter.top - m.desktopRect.top;

            for (int row = 0; row < rh; ++row) {
                for (int col = 0; col < rw; ++col) {
                    int rx = rx0 + col;
                    int ry = ry0 + row;

                    int u = rx, v = ry;
                    switch (m.rotation) {
                    case DXGI_MODE_ROTATION_ROTATE90:
                        u = ry;
                        v = static_cast<int>(m.width) - rx - 1;
                        break;
                    case DXGI_MODE_ROTATION_ROTATE180:
                        u = static_cast<int>(m.width) - rx - 1;
                        v = static_cast<int>(m.height) - ry - 1;
                        break;
                    case DXGI_MODE_ROTATION_ROTATE270:
                        u = static_cast<int>(m.height) - ry - 1;
                        v = rx;
                        break;
                    default:
                        break;
                    }

                    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData) + v * mapped.RowPitch + u * bpp;
                    uint8_t* dst = out.data.data() + ((destY + row) * w + destX + col) * bpp;
                    memcpy(dst, src, bpp);
                }
            }
        }

        if (!gotFrame || !allSuccess) {
            return CaptureResult::NeedsReinitialize;
        }
        
        if (out.data.empty() || std::all_of(out.data.begin(), out.data.end(), [](uint8_t v) { return v == 0; })) {
            return CaptureResult::TemporaryFailure;
        }

        return CaptureResult::Success;
    }

} // namespace screenshot_tool