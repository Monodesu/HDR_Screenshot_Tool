#include "SmartCapture.hpp"
#include <thread>

namespace screenshot_tool {

    // ---- 初始化 ---------------------------------------------------------------
    bool SmartCapture::Initialize()
    {
        Logger::Debug(L"SmartCapture::Initialize()");
        // DXGI 初始化失败后自动 fallback
        dxgi_.Initialize();
        return true;
    }

    // ---- 捕获窗口：CaptureToFileAndClipboard -----------------------------------
    SmartCapture::Result SmartCapture::CaptureToFileAndClipboard(HWND hwnd, const RECT& r,
        const wchar_t* savePath)
    {
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        if (w <= 0 || h <= 0) return Result::Failed;

        ImageBuffer rgb8;
        bool usedGDI = false;
        if (!captureRegionInternal(r.left, r.top, w, h, rgb8, usedGDI)) {
            Logger::Error(L"SmartCapture: captureRegionInternal failed");
            return Result::Failed;
        }

        // 写入剪贴板
        if (!ClipboardWriter::WriteRGB(hwnd, rgb8.data.data(), w, h)) {
            Logger::Warn(L"ClipboardWriter failed");
        }

        // PNG 保存（路径为空则不保存）
        if (savePath && *savePath) {
            if (!ImageSaverPNG::SaveRGBToPNG(rgb8.data.data(), w, h, savePath)) {
                Logger::Warn(L"ImageSaverPNG failed");
            }
        }

        return usedGDI ? Result::FallbackGDI : Result::OK;
    }

    // ---- 全屏截图 (捕获整个虚拟桌面) --------------------------------------
    SmartCapture::Result SmartCapture::CaptureFullscreen(HWND hwnd, const RECT& virtualRect,
        const wchar_t* savePath)
    {
        return CaptureToFileAndClipboard(hwnd, virtualRect, savePath);
    }

    // ---- 内部捕获逻辑：DXGI 或 失败时回退到 GDI ----------------------------------
    bool SmartCapture::captureRegionInternal(int x, int y, int w, int h,
        ImageBuffer& outRGB8,
        bool& usedGDI)
    {
        usedGDI = false;

        // 优先尝试 DXGI
        if (dxgi_.IsInitialized()) {
            DXGI_FORMAT fmt{};
            CaptureResult result = dxgi_.CaptureRegion(x, y, w, h, fmt, outRGB8);
            
            if (result == CaptureResult::Success) {
                // HDR 转 SDR，传递 HDR 状态和配置
                // 只有在实际获取到HDR格式数据时才进行HDR处理
                bool isHDR = dxgi_.IsHDREnabled() && 
                            (fmt == DXGI_FORMAT_R16G16B16A16_FLOAT || 
                             fmt == DXGI_FORMAT_R10G10B10A2_UNORM);
                PixelConvert::ToSRGB8(fmt, outRGB8, isHDR, cfg_);
                return true;
            }
            else if (result == CaptureResult::NeedsReinitialization) {
                Logger::Warn(L"DXGI needs reinitialization, fallback to GDI");
                dxgi_.Reinitialize(); // 尝试重新初始化，下一次可能恢复
            }
            else {
                Logger::Warn(L"DXGI Capture failed, fallback to GDI");
            }
        }

        // GDI fallback
        usedGDI = true;
        if (gdi_.CaptureRegion(x, y, w, h, outRGB8)) {
            return true;
        }

        return false;
    }

    RECT SmartCapture::GetVirtualDesktop() const {
        // 优先使用DXGI计算的虚拟桌面，fallback到GetSystemMetrics
        if (dxgi_.IsInitialized()) {
            return dxgi_.GetVirtualRect();
        }
        
        // GDI fallback
        return RECT{ 
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)
        };
    }

    bool SmartCapture::CaptureFullscreenToCache() {
        RECT vr = GetVirtualDesktop();
        int w = vr.right - vr.left;
        int h = vr.bottom - vr.top;
        
        cachedFullscreen_ = {};
        cachedFormat_ = DXGI_FORMAT_UNKNOWN;
        hasCachedData_ = false;
        
        // 优先尝试 DXGI 捕获全屏原始数据
        if (dxgi_.IsInitialized()) {
            CaptureResult result = dxgi_.CaptureRegion(vr.left, vr.top, w, h, cachedFormat_, cachedFullscreen_);
            
            if (result == CaptureResult::Success) {
                hasCachedData_ = true;
                Logger::Info(L"Cached fullscreen data via DXGI: {}x{}, format: {}", w, h, static_cast<int>(cachedFormat_));
                return true;
            }
            else if (result == CaptureResult::NeedsReinitialization) {
                Logger::Warn(L"DXGI needs reinitialization for cache");
                dxgi_.Reinitialize();
            }
        }
        
        // GDI fallback - 直接获取 RGB8 格式
        if (gdi_.CaptureRegion(vr.left, vr.top, w, h, cachedFullscreen_)) {
            cachedFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM; // GDI 输出的是 RGB8
            hasCachedData_ = true;
            Logger::Info(L"Cached fullscreen data via GDI: {}x{}", w, h);
            return true;
        }
        
        Logger::Error(L"Failed to cache fullscreen data");
        return false;
    }

    SmartCapture::Result SmartCapture::ExtractRegionFromCache(HWND hwnd, const RECT& r, const wchar_t* savePath) {
        if (!hasCachedData_) {
            Logger::Error(L"No cached data available for region extraction");
            return Result::Failed;
        }
        
        RECT vr = GetVirtualDesktop();
        int cacheWidth = vr.right - vr.left;
        int cacheHeight = vr.bottom - vr.top;
        
        // 计算区域在缓存中的位置
        int regionX = r.left - vr.left;
        int regionY = r.top - vr.top;
        int regionW = r.right - r.left;
        int regionH = r.bottom - r.top;
        
        // 边界检查
        if (regionX < 0 || regionY < 0 || 
            regionX + regionW > cacheWidth || regionY + regionH > cacheHeight) {
            Logger::Error(L"Region out of cached bounds");
            return Result::Failed;
        }
        
        // 从缓存中提取区域数据
        ImageBuffer regionBuffer;
        regionBuffer.format = cachedFullscreen_.format;
        regionBuffer.width = regionW;
        regionBuffer.height = regionH;
        
        int pixelSize = (cachedFullscreen_.format == PixelFormat::RGBA_F16) ? 8 : 
                       (cachedFullscreen_.format == PixelFormat::RGBA10A2) ? 4 : 
                       (cachedFullscreen_.format == PixelFormat::BGRA8) ? 4 : 3;
        
        regionBuffer.stride = regionW * pixelSize;
        regionBuffer.data.resize(regionBuffer.stride * regionH);
        
        // 逐行复制区域数据
        for (int y = 0; y < regionH; ++y) {
            const uint8_t* srcRow = cachedFullscreen_.data.data() + 
                                   (regionY + y) * cachedFullscreen_.stride + 
                                   regionX * pixelSize;
            uint8_t* dstRow = regionBuffer.data.data() + y * regionBuffer.stride;
            memcpy(dstRow, srcRow, regionBuffer.stride);
        }
        
        // 处理 HDR 转换
        // 注意：如果使用GDI fallback捕获的数据，即使显示器支持HDR，数据也是SDR的
        bool isHDR = dxgi_.IsHDREnabled() && 
                    (cachedFormat_ == DXGI_FORMAT_R16G16B16A16_FLOAT || 
                     cachedFormat_ == DXGI_FORMAT_R10G10B10A2_UNORM);
        PixelConvert::ToSRGB8(cachedFormat_, regionBuffer, isHDR, cfg_);
        
        // 写入剪贴板
        bool clipboardSuccess = ClipboardWriter::WriteRGB(hwnd, regionBuffer.data.data(), regionW, regionH);
        if (!clipboardSuccess) {
            Logger::Warn(L"ClipboardWriter failed");
        }
        
        // PNG 保存（路径为空则不保存）
        bool fileSuccess = true;
        if (savePath && *savePath) {
            fileSuccess = ImageSaverPNG::SaveRGBToPNG(regionBuffer.data.data(), regionW, regionH, savePath);
            if (!fileSuccess) {
                Logger::Warn(L"ImageSaverPNG failed");
            }
        }
        
        return (clipboardSuccess && fileSuccess) ? Result::OK : Result::Failed;
    }
    
    bool SmartCapture::GetCachedImageAsRGB8(ImageBuffer& outRGB8) const {
        if (!hasCachedData_) {
            Logger::Error(L"No cached data available");
            return false;
        }
        
        // 如果已经是RGB8格式，直接复制
        if (cachedFullscreen_.format == PixelFormat::RGB8) {
            outRGB8 = cachedFullscreen_;
            return true;
        }
        
        // 需要格式转换
        outRGB8.format = PixelFormat::RGB8;
        outRGB8.width = cachedFullscreen_.width;
        outRGB8.height = cachedFullscreen_.height;
        outRGB8.stride = outRGB8.width * 3; // RGB8格式每像素3字节
        outRGB8.data.resize(outRGB8.stride * outRGB8.height);
        
        // 复制原始数据到临时缓冲区进行转换
        ImageBuffer tempBuffer = cachedFullscreen_;
        
        // 使用PixelConvert进行格式转换
        // 注意：只有在实际获取到HDR格式数据时才进行HDR处理
        bool isHDR = dxgi_.IsHDREnabled() && 
                    (cachedFormat_ == DXGI_FORMAT_R16G16B16A16_FLOAT || 
                     cachedFormat_ == DXGI_FORMAT_R10G10B10A2_UNORM);
        
        // 转换为sRGB8格式
        PixelConvert::ToSRGB8(cachedFormat_, tempBuffer, isHDR, cfg_);
        
        // 将转换后的数据复制到输出缓冲区
        outRGB8 = tempBuffer;
        outRGB8.format = PixelFormat::RGB8;
        
        Logger::Debug(L"Converted cached image to RGB8 format: {}x{}", outRGB8.width, outRGB8.height);
        return true;
    }

} // namespace screenshot_tool
