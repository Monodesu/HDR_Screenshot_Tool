#pragma once

#include "DXGICapture.hpp"
#include "GDICapture.hpp"
#include "../config/Config.hpp"
#include "../image/PixelConvert.hpp"
#include "../image/ClipboardWriter.hpp"
#include "../image/ImageSaverPNG.hpp"
#include "../util/PathUtils.hpp"
#include "../util/Logger.hpp"

namespace screenshot_tool {

    class SmartCapture {
    public:
        enum class Result {
            OK,            // 成功，使用 DXGI
            FallbackGDI,   // 成功，但 DXGI 失败后使用 GDI
            Failed         // 两种后端都失败
        };

        explicit SmartCapture(Config* cfg) : cfg_(cfg), dxgi_(), gdi_() {}

        // ---- 初始化 -------------------------------------------------------------
        bool Initialize();            // 必须在捕获前调用一次（App 启动时）

        // ---- 主入口 -------------------------------------------------------------
        Result CaptureToFileAndClipboard(HWND hwnd, const RECT& r, const wchar_t* savePath);
        Result CaptureFullscreen(HWND hwnd, const RECT& virtualRect, const wchar_t* savePath);
        
        // ---- 冻结帧区域截图 -----------------------------------------------------
        bool CaptureFullscreenToCache();  // 捕获全屏到缓存
        Result ExtractRegionFromCache(HWND hwnd, const RECT& r, const wchar_t* savePath);  // 从缓存提取区域
        
        // ---- 工具方法 -----------------------------------------------------------
        RECT GetVirtualDesktop() const;
        
        // ---- 访问缓存数据 -------------------------------------------------------
        const ImageBuffer* GetCachedImage() const { return hasCachedData_ ? &cachedFullscreen_ : nullptr; }
        bool HasCachedData() const { return hasCachedData_; }
        
        // 获取缓存图像的RGB8版本（用于overlay背景显示）
        bool GetCachedImageAsRGB8(ImageBuffer& outRGB8) const;

    private:
        // 区域抓屏到 ImageBuffer (8-bit RGB)
        bool captureRegionInternal(int x, int y, int w, int h, ImageBuffer& outRGB8,
            bool& usedGDI);

        Config* cfg_ = nullptr;
        DXGICapture  dxgi_;
        GDICapture   gdi_;
        
        // 冻结帧缓存
        ImageBuffer cachedFullscreen_;
        DXGI_FORMAT cachedFormat_ = DXGI_FORMAT_UNKNOWN;
        bool hasCachedData_ = false;
    };

} // namespace screenshot_tool