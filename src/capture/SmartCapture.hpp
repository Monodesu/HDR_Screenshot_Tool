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
        SmartCapture(Config* cfg) : cfg_(cfg) {}
        bool Initialize();

        // 主入口：抓区域，完成格式转换、写剪贴板、可选保存文件
        bool CaptureRegionAndOutput(HWND hwnd, const RECT& r, std::wstring* outSavedPath = nullptr);

        // 全屏（虚拟桌面 / 当前监视器，根据 cfg）
        bool CaptureFullscreenAndOutput(HWND hwnd, std::wstring* outSavedPath = nullptr);

    private:
        bool captureRegionInternal(int x, int y, int w, int h, ImageBuffer& outRGB8);

        Config* cfg_ = nullptr;
        DXGICapture dxgi_;
        GDICapture  gdi_;
    };

} // namespace screenshot_tool