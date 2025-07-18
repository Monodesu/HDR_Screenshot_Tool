#include "SelectionOverlay.hpp"
#include <algorithm>
#include <vector>
#include <cmath>
#include "../util/Logger.hpp"

// D3D11 和 D2D/DirectWrite 头文件
#include <d3d11_1.h>
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>

// 链接必要的库
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#include <mmsystem.h>

using Microsoft::WRL::ComPtr;

namespace screenshot_tool {

    static const wchar_t* kOverlayClass = L"ScreenShotOverlayWindow";
    
    // 颜色常量定义
    namespace OverlayColors {
        static constexpr COLORREF TRANSPARENT_KEY = RGB(255, 0, 255);
        static constexpr COLORREF DARKEN_BASE = RGB(0, 0, 0);
        static constexpr COLORREF TEXT_BACKGROUND = RGB(40, 40, 40);
        static constexpr COLORREF SELECTION_BORDER = RGB(255, 255, 255);
        static constexpr COLORREF TEXT_COLOR = RGB(255, 255, 255);
    }

    bool SelectionOverlay::Create(HINSTANCE inst, HWND parent, Callback cb) {
        parent_ = parent; 
        cb_ = std::move(cb);
        
        WNDCLASS wc{};
        wc.lpfnWndProc = SelectionOverlay::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = kOverlayClass;
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        RegisterClass(&wc);
        
        // 使用分层窗口，创建时不显示
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 
                               kOverlayClass, L"", WS_POPUP, 
                               0, 0, 0, 0, parent, nullptr, inst, this);
        
        if (hwnd_) {
            // 设置初始透明状态，避免闪烁
            SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, 0, LWA_COLORKEY | LWA_ALPHA);
            
            // D3D/D2D渲染系统是必需的 - 不再有回退选项
            if (!initializeD3DRenderer()) {
                Logger::Error(L"Failed to initialize D3D renderer - this is required!");
                return false; // 直接返回失败，不再回退
            }
            
            initializeSimpleAnimation();
            
            // *** 修复：尝试加载背景图像，避免首次黑屏 ***
            if (backgroundCheckCallback_) {
                backgroundCheckCallback_();
            }
            
            // 延迟显示窗口，确保初始化完成后再显示
            ShowWindow(hwnd_, SW_HIDE);
        }
        
        return hwnd_ != nullptr;
    }

    SelectionOverlay::~SelectionOverlay() {
        // 清理定时器
        if (timerId_ && hwnd_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        if (backgroundCheckTimerId_ && hwnd_) {
            KillTimer(hwnd_, backgroundCheckTimerId_);
            backgroundCheckTimerId_ = 0;
        }
        
        // 清理D3D渲染资源
        cleanupD3DRenderer();
        
        // 清理背景图像资源
        destroyBackgroundBitmap();
        
        // 销毁窗口
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    bool SelectionOverlay::initializeD3DRenderer() {
        // 获取窗口尺寸
        RECT clientRect;
        GetClientRect(hwnd_, &clientRect);
        UINT width = clientRect.right - clientRect.left;
        UINT height = clientRect.bottom - clientRect.top;
        
        if (width == 0 || height == 0) {
            width = 1920; // 默认尺寸
            height = 1080;
        }

        // 创建D3D11设备和交换链
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 2;
        swapChainDesc.BufferDesc.Width = width;
        swapChainDesc.BufferDesc.Height = height;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = hwnd_;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &dxgiSwapChain_,
            &d3dDevice_,
            &d3dFeatureLevel_,
            &d3dContext_
        );

        if (FAILED(hr)) {
            Logger::Error(L"Failed to create D3D11 device and swap chain: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        // 创建渲染目标视图
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = dxgiSwapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            Logger::Error(L"Failed to get back buffer: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        hr = d3dDevice_->CreateRenderTargetView(backBuffer.Get(), nullptr, &d3dRenderTargetView_);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create render target view: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        // 创建D2D工厂
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create D2D factory: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        // 创建DirectWrite工厂
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &dwriteFactory_);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create DirectWrite factory: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        // 创建D2D渲染目标
        ComPtr<IDXGISurface> dxgiSurface;
        hr = dxgiSwapChain_->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
        if (FAILED(hr)) {
            Logger::Error(L"Failed to get DXGI surface: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f
        );

        hr = d2dFactory_->CreateDxgiSurfaceRenderTarget(dxgiSurface.Get(), &rtProps, &d2dRenderTarget_);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create D2D render target: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        // 创建画刷
        hr = d2dRenderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White), &d2dWhiteBrush_);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create white brush: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        hr = d2dRenderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(0.16f, 0.16f, 0.16f, 0.8f), &d2dDarkBrush_);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create dark brush: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        // 创建文字格式
        hr = dwriteFactory_->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            16.0f,
            L"en-us",
            &dwriteTextFormat_
        );
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create text format: {:#x}", static_cast<uint32_t>(hr));
            return false;
        }

        dwriteTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        dwriteTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        Logger::Info(L"D3D/D2D renderer initialized successfully");
        return true;
    }

    void SelectionOverlay::cleanupD3DRenderer() {
        dwriteTextFormat_.Reset();
        d2dDarkBrush_.Reset();
        d2dWhiteBrush_.Reset();
        d2dRenderTarget_.Reset();
        dwriteFactory_.Reset();
        d2dFactory_.Reset();
        d3dRenderTargetView_.Reset();
        d3dContext_.Reset();
        d3dDevice_.Reset();
        dxgiSwapChain_.Reset();
    }

    void SelectionOverlay::resizeD3DRenderer(UINT width, UINT height) {
        if (!dxgiSwapChain_) return;

        // 清理旧的渲染目标
        d2dRenderTarget_.Reset();
        d3dRenderTargetView_.Reset();
        d3dContext_->Flush();

        // 调整交换链大小
        HRESULT hr = dxgiSwapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to resize swap chain buffers: {:#x}", static_cast<uint32_t>(hr));
            return;
        }

        // 重新创建渲染目标视图
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = dxgiSwapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            Logger::Error(L"Failed to get back buffer after resize: {:#x}", static_cast<uint32_t>(hr));
            return;
        }

        hr = d3dDevice_->CreateRenderTargetView(backBuffer.Get(), nullptr, &d3dRenderTargetView_);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create render target view after resize: {:#x}", static_cast<uint32_t>(hr));
            return;
        }

        // 重新创建D2D渲染目标
        ComPtr<IDXGISurface> dxgiSurface;
        hr = dxgiSwapChain_->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
        if (FAILED(hr)) {
            Logger::Error(L"Failed to get DXGI surface after resize: {:#x}", static_cast<uint32_t>(hr));
            return;
        }

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f
        );

        hr = d2dFactory_->CreateDxgiSurfaceRenderTarget(dxgiSurface.Get(), &rtProps, &d2dRenderTarget_);
        if (FAILED(hr)) {
            Logger::Error(L"Failed to create D2D render target after resize: {:#x}", static_cast<uint32_t>(hr));
            return;
        }

        // 重新创建画刷
        d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &d2dWhiteBrush_);
        d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.16f, 0.16f, 0.8f), &d2dDarkBrush_);
    }

    void SelectionOverlay::initializeSimpleAnimation() {
        fadeInterval_ = 15;
        alpha_ = 0;
        fadingIn_ = false;
        fadingOut_ = false;
        lastUpdateTime_ = GetTickCount();
        
        Logger::Debug(L"Animation system initialized with 15ms interval");
    }

    void SelectionOverlay::Hide() {
        if (!hwnd_) return;
        
        if (backgroundCheckTimerId_) {
            KillTimer(hwnd_, backgroundCheckTimerId_);
            backgroundCheckTimerId_ = 0;
        }
        
        if (selecting_) {
            if (timerId_) {
                KillTimer(hwnd_, timerId_);
                timerId_ = 0;
            }
            fadingIn_ = false;
            fadingOut_ = false;
            ShowWindow(hwnd_, SW_HIDE);
        } else {
            startFadeOut();
        }

        // *** 清理背景图像缓存 ***
        destroyBackgroundBitmap();
    }

    void SelectionOverlay::ShowWithRect(const RECT& displayRect) {
        if (!hwnd_) return; 
        
        // 清除之前的选择状态
        selecting_ = false;
        start_.x = start_.y = 0;
        cur_.x = cur_.y = 0;
        memset(&selectedRect_, 0, sizeof(selectedRect_));
        notifyOnHide_ = false;
        
        // *** 先清理旧的背景图像，然后等待新的冻结画面加载完成 ***
        destroyBackgroundBitmap(); // 确保清理掉上次的残留画面
        
        if (backgroundCheckCallback_) {
            backgroundCheckCallback_(); // 加载新的背景图像
        }
        
        // 重置动画状态
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        if (backgroundCheckTimerId_) {
            KillTimer(hwnd_, backgroundCheckTimerId_);
            backgroundCheckTimerId_ = 0;
        }
        fadingIn_ = false;
        fadingOut_ = false;
        alpha_ = 0;
        
        ShowWindow(hwnd_, SW_HIDE);
        
        // 存储虚拟桌面偏移信息
        virtualDesktopLeft_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtualDesktopTop_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
        
        // 设置窗口位置和大小
        SetWindowPos(hwnd_, HWND_TOPMOST, 
                    displayRect.left, displayRect.top, 
                    displayRect.right - displayRect.left, 
                    displayRect.bottom - displayRect.top, 
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        
        // 调整D3D渲染器大小
        resizeD3DRenderer(displayRect.right - displayRect.left, displayRect.bottom - displayRect.top);
        
        // 设置窗口完全透明，等待背景图像加载
        SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, 0, LWA_COLORKEY | LWA_ALPHA);
        
        // *** 确保冻结画面加载完成后再显示窗口和暗化效果 ***
        if (backgroundBitmap_) {
            SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, 255, LWA_COLORKEY | LWA_ALPHA);
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        }
        
        SetFocus(hwnd_);
    }

    void SelectionOverlay::startFadeIn() {
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        alpha_ = 0;
        fadingIn_ = true;
        fadingOut_ = false;
        
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, alpha_, LWA_COLORKEY | LWA_ALPHA);
        
        timerId_ = SetTimer(hwnd_, FADE_TIMER_ID, fadeInterval_, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void SelectionOverlay::startFadeOut() {
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        fadingIn_ = false;
        fadingOut_ = true;
        
        // 如果当前alpha为0，直接隐藏
        if (alpha_ <= 0) {
            alpha_ = 0;
            fadingOut_ = false;
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }
        
        timerId_ = SetTimer(hwnd_, FADE_TIMER_ID, fadeInterval_, nullptr);
    }

    void SelectionOverlay::updateFade() {
        if (fadingIn_) {
            // 根据是否有背景图像决定目标透明度
            BYTE targetAlpha = backgroundBitmap_ ? 255 : TARGET_ALPHA;
            
            alpha_ = static_cast<BYTE>(std::min<int>(alpha_ + 16, targetAlpha));
            SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, alpha_, LWA_COLORKEY | LWA_ALPHA);
            
            if (alpha_ >= targetAlpha) {
                fadingIn_ = false;
                KillTimer(hwnd_, timerId_);
                timerId_ = 0;
            }
        } else if (fadingOut_) {
            if (alpha_ <= 16) {
                alpha_ = 0;
                SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, alpha_, LWA_COLORKEY | LWA_ALPHA);
                fadingOut_ = false;
                KillTimer(hwnd_, timerId_);
                timerId_ = 0;
                ShowWindow(hwnd_, SW_HIDE);
                
                auto style = GetWindowLong(hwnd_, GWL_EXSTYLE);
                SetWindowLong(hwnd_, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
                selecting_ = false;
                
                if (notifyOnHide_ && parent_) {
                    PostMessage(parent_, WM_USER + 100, 0, 0);
                }
                notifyOnHide_ = false;
            } else {
                alpha_ = static_cast<BYTE>(alpha_ - 16);
                SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, alpha_, LWA_COLORKEY | LWA_ALPHA);
            }
        } else {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
    }

    // ---- D3D渲染实现 - 唯一的渲染路径 ----
    void SelectionOverlay::renderWithD3D() {
        if (!d2dRenderTarget_) return;

        // 开始D2D绘制
        d2dRenderTarget_->BeginDraw();

        // 清除背景为透明（用于分层窗口）
        d2dRenderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        // 如果有背景图像，绘制背景
        if (backgroundBitmap_) {
            renderBackgroundWithD3D();
        }

        // 绘制暗化遮罩层（除了选择区域）
        renderDarkenMaskWithD3D();

        // 如果正在选择，绘制选择框和尺寸信息
        if (selecting_) {
            renderSelectionBoxWithD3D();
            renderSizeTextWithD3D();
        }

        // 结束D2D绘制
        HRESULT hr = d2dRenderTarget_->EndDraw();
        if (FAILED(hr)) {
            Logger::Error(L"D2D EndDraw failed: {:#x}", static_cast<uint32_t>(hr));
            return;
        }

        // 显示到屏幕
        hr = dxgiSwapChain_->Present(0, 0);
        if (FAILED(hr)) {
            Logger::Error(L"DXGI Present failed: {:#x}", static_cast<uint32_t>(hr));
        }
    }

    void SelectionOverlay::renderBackgroundWithD3D() {
        if (!d2dRenderTarget_ || !backgroundBitmap_) return;

        // 创建D2D位图从GDI 位图
        ComPtr<ID2D1Bitmap> d2dBitmap;
        createD2DBitmapFromGDI(backgroundBitmap_, &d2dBitmap);
        
        if (d2dBitmap) {
            RECT clientRect;
            GetClientRect(hwnd_, &clientRect);
            
            D2D1_RECT_F destRect = D2D1::RectF(
                0.0f, 0.0f, 
                static_cast<float>(clientRect.right), 
                static_cast<float>(clientRect.bottom)
            );
            
            // 绘制背景图像，拉伸到窗口大小
            d2dRenderTarget_->DrawBitmap(d2dBitmap.Get(), destRect, 1.0f, 
                                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }

    void SelectionOverlay::renderDarkenMaskWithD3D() {
        if (!d2dRenderTarget_ || !backgroundBitmap_) return; // 仅在背景图片加载完成后显示暗化效果

        RECT clientRect;
        GetClientRect(hwnd_, &clientRect);

        // 创建半透明暗化画刷
        ComPtr<ID2D1SolidColorBrush> darkenBrush;
        HRESULT hr = d2dRenderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f), &darkenBrush);
        
        if (FAILED(hr)) return;

        D2D1_RECT_F fullRect = D2D1::RectF(
            0.0f, 0.0f, 
            static_cast<float>(clientRect.right), 
            static_cast<float>(clientRect.bottom)
        );

        if (selecting_) {
            // 绘制除选择区域外的暗化遮罩
            D2D1_RECT_F selectionRect = D2D1::RectF(
                static_cast<float>(std::min(start_.x, cur_.x)),
                static_cast<float>(std::min(start_.y, cur_.y)),
                static_cast<float>(std::max(start_.x, cur_.x)),
                static_cast<float>(std::max(start_.y, cur_.y))
            );

            // 使用几何形状创建镂空效果
            ComPtr<ID2D1PathGeometry> pathGeometry;
            hr = d2dFactory_->CreatePathGeometry(&pathGeometry);
            if (FAILED(hr)) return;

            ComPtr<ID2D1GeometrySink> geometrySink;
            hr = pathGeometry->Open(&geometrySink);
            if (FAILED(hr)) return;

            // 创建外部矩形，内部镂空选择区域
            geometrySink->SetFillMode(D2D1_FILL_MODE_WINDING);
            
            // 外部矩形
            geometrySink->BeginFigure(D2D1::Point2F(fullRect.left, fullRect.top), D2D1_FIGURE_BEGIN_FILLED);
            geometrySink->AddLine(D2D1::Point2F(fullRect.right, fullRect.top));
            geometrySink->AddLine(D2D1::Point2F(fullRect.right, fullRect.bottom));
            geometrySink->AddLine(D2D1::Point2F(fullRect.left, fullRect.bottom));
            geometrySink->EndFigure(D2D1_FIGURE_END_CLOSED);

            // 内部镂空矩形（选择区域）
            geometrySink->BeginFigure(D2D1::Point2F(selectionRect.left, selectionRect.top), D2D1_FIGURE_BEGIN_FILLED);
            geometrySink->AddLine(D2D1::Point2F(selectionRect.left, selectionRect.bottom));
            geometrySink->AddLine(D2D1::Point2F(selectionRect.right, selectionRect.bottom));
            geometrySink->AddLine(D2D1::Point2F(selectionRect.right, selectionRect.top));
            geometrySink->EndFigure(D2D1_FIGURE_END_CLOSED);

            hr = geometrySink->Close();
            if (FAILED(hr)) return;

            // 填充镂空的暗化区域
            d2dRenderTarget_->FillGeometry(pathGeometry.Get(), darkenBrush.Get());
        } else {
            // 没有选择时，全屏暗化
            d2dRenderTarget_->FillRectangle(fullRect, darkenBrush.Get());
        }
    }

    void SelectionOverlay::renderSelectionBoxWithD3D() {
        if (!d2dRenderTarget_ || !selecting_) return;

        D2D1_RECT_F selectionRect = D2D1::RectF(
            static_cast<float>(std::min(start_.x, cur_.x)),
            static_cast<float>(std::min(start_.y, cur_.y)),
            static_cast<float>(std::max(start_.x, cur_.x)),
            static_cast<float>(std::max(start_.y, cur_.y))
        );

        // 使用预创建的白色画刷绘制选择框边框
        if (d2dWhiteBrush_) {
            d2dRenderTarget_->DrawRectangle(selectionRect, d2dWhiteBrush_.Get(), 2.0f);
        }
    }

    void SelectionOverlay::renderSizeTextWithD3D() {
        if (!d2dRenderTarget_ || !dwriteTextFormat_ || !selecting_) return;

        int width = abs(cur_.x - start_.x);
        int height = abs(cur_.y - start_.y);

        // 格式化尺寸文本
        wchar_t sizeText[64];
        swprintf_s(sizeText, L"%d × %d", width, height);

        // 计算文本布局
        ComPtr<IDWriteTextLayout> textLayout;
        HRESULT hr = dwriteFactory_->CreateTextLayout(
            sizeText,
            static_cast<UINT32>(wcslen(sizeText)),
            dwriteTextFormat_.Get(),
            200.0f,  // 最大宽度
            50.0f,   // 最大高度
            &textLayout
        );

        if (FAILED(hr)) return;

        // 获取文本尺寸
        DWRITE_TEXT_METRICS textMetrics;
        hr = textLayout->GetMetrics(&textMetrics);
        if (FAILED(hr)) return;

        // 计算文本位置
        float textX = static_cast<float>(std::min(start_.x, cur_.x));
        float textY = static_cast<float>(std::min(start_.y, cur_.y)) - textMetrics.height - 8.0f;

        // 如果文本会显示在顶部之外，则显示在选择框内部
        if (textY < 5.0f) {
            textY = static_cast<float>(std::min(start_.y, cur_.y)) + 8.0f;
        }

        // 确保文本不会超出左边界
        if (textX < 5.0f) textX = 5.0f;

        // 创建文本背景矩形
        D2D1_RECT_F textBgRect = D2D1::RectF(
            textX - 6.0f, textY - 3.0f,
            textX + textMetrics.width + 6.0f, textY + textMetrics.height + 3.0f
        );

        // 绘制半透明的文本背景
        if (d2dDarkBrush_) {
            // 绘制圆角背景矩形
            ComPtr<ID2D1RoundedRectangleGeometry> roundedRect;
            hr = d2dFactory_->CreateRoundedRectangleGeometry(
                D2D1::RoundedRect(textBgRect, 6.0f, 6.0f), &roundedRect);
            
            if (SUCCEEDED(hr)) {
                d2dRenderTarget_->FillGeometry(roundedRect.Get(), d2dDarkBrush_.Get());
            }
        }

        // 绘制文本
        if (d2dWhiteBrush_) {
            d2dRenderTarget_->DrawTextLayout(
                D2D1::Point2F(textX, textY),
                textLayout.Get(),
                d2dWhiteBrush_.Get()
            );
        }
    }

    void SelectionOverlay::createD2DBitmapFromGDI(HBITMAP gdiBitmap, ID2D1Bitmap** d2dBitmap) {
        if (!d2dRenderTarget_ || !gdiBitmap || !d2dBitmap) return;

        *d2dBitmap = nullptr;

        // 获取GDI位图信息
        BITMAP bm;
        if (GetObject(gdiBitmap, sizeof(BITMAP), &bm) == 0) return;

        // 创建兼容的DC和选择位图
        HDC memDC = CreateCompatibleDC(nullptr);
        if (!memDC) return;

        HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, gdiBitmap));

        // 创建DIB数据
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bm.bmWidth;
        bmi.bmiHeader.biHeight = -bm.bmHeight; // 负数表示从上到下
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        std::vector<uint8_t> pixelData(bm.bmWidth * bm.bmHeight * 4);
        
        if (GetDIBits(memDC, gdiBitmap, 0, bm.bmHeight, pixelData.data(), &bmi, DIB_RGB_COLORS)) {
            // 创建D2D位图属性
            D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96.0f, 96.0f
            );

            // 创建D2D位图
            HRESULT hr = d2dRenderTarget_->CreateBitmap(
                D2D1::SizeU(bm.bmWidth, bm.bmHeight),
                pixelData.data(),
                bm.bmWidth * 4,
                &bitmapProps,
                d2dBitmap
            );

            if (FAILED(hr)) {
                Logger::Error(L"Failed to create D2D bitmap: {:#x}", static_cast<uint32_t>(hr));
            }
        }

        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    }

    // ---- 窗口消息处理 - 只使用D3D渲染 ----
    LRESULT SelectionOverlay::instanceProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_LBUTTONDOWN: 
            startSelect(GET_X_LPARAM(l), GET_Y_LPARAM(l)); 
            return 0;
            
        case WM_MOUSEMOVE:   
            if (selecting_) updateSelect(GET_X_LPARAM(l), GET_Y_LPARAM(l)); 
            return 0;
            
        case WM_LBUTTONUP:   
            finishSelect(); 
            return 0;
            
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            cancelSelection();
            return 0;
            
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) {
                cancelSelection();
            }
            return 0;
            
        case WM_TIMER:
            if (w == FADE_TIMER_ID) {
                updateFade();
            } else if (w == BACKGROUND_CHECK_TIMER_ID) {
                if (backgroundCheckCallback_) {
                    backgroundCheckCallback_();
                } else {
                    SetBackgroundImage(nullptr, 0, 0, 0);
                }
            }
            return 0;
            
        case WM_PAINT: { 
            PAINTSTRUCT ps; 
            BeginPaint(h, &ps); 
            
            // 只使用D3D渲染，不再有GDI回退
            renderWithD3D();
            
            EndPaint(h, &ps); 
            return 0;
        }

        case WM_SIZE: {
            UINT width = LOWORD(l);
            UINT height = HIWORD(l);
            if (width > 0 && height > 0) {
                resizeD3DRenderer(width, height);
            }
            return 0;
        }
        
        default:
            return DefWindowProc(h, m, w, l);
        }
    }

    void SelectionOverlay::BeginSelect() {
        // 获取整个虚拟桌面区域
        RECT virtualDesktop;
        virtualDesktop.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtualDesktop.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        virtualDesktop.right = virtualDesktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        virtualDesktop.bottom = virtualDesktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        
        useMonitorConstraint_ = false;
        ShowWithRect(virtualDesktop);
        
        // 不在这里启动淡入动画 - 等待SetBackgroundImage中处理
        // 只有在背景图像已经存在的情况下才启动动画
        if (backgroundBitmap_) {
            auto style = GetWindowLong(hwnd_, GWL_EXSTYLE);
            SetWindowLong(hwnd_, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
            startFadeIn();
        }
    }

    void SelectionOverlay::BeginSelectOnMonitor(const RECT& monitorRect) {
        useMonitorConstraint_ = true;
        monitorConstraint_ = monitorRect;
        ShowWithRect(monitorRect);
        
        // 不在这里启动淡入动画 - 等待SetBackgroundImage中处理
        // 只有在背景图像已经存在的情况下才启动动画
        if (backgroundBitmap_) {
            auto style = GetWindowLong(hwnd_, GWL_EXSTYLE);
            SetWindowLong(hwnd_, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
            startFadeIn();
        }
    }

    void SelectionOverlay::StartWaitingForBackground(std::function<void()> backgroundCheckCallback) {
        backgroundCheckCallback_ = std::move(backgroundCheckCallback);
        
        if (hwnd_) {
            backgroundCheckTimerId_ = SetTimer(hwnd_, BACKGROUND_CHECK_TIMER_ID, BACKGROUND_CHECK_INTERVAL, nullptr);
        }
    }

    void SelectionOverlay::startSelect(int x, int y) { 
        selecting_ = true; 
        start_.x = x; 
        start_.y = y; 
        cur_ = start_; 
        
        SetCapture(hwnd_);
        
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        fadingIn_ = false;
        fadingOut_ = false;
        alpha_ = 255;  // 选择时完全不透明
        
        SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, alpha_, LWA_COLORKEY | LWA_ALPHA);
        
        InvalidateRect(hwnd_, nullptr, FALSE); 
    }
    
    void SelectionOverlay::updateSelect(int x, int y) { 
        // 性能优化：限制鼠标移动更新频率
        DWORD currentTime = GetTickCount();
        if (currentTime - lastUpdateTime_ < MIN_UPDATE_INTERVAL) {
            return;
        }
        lastUpdateTime_ = currentTime;
        
        // 只在坐标真正变化时才重绘
        if (cur_.x != x || cur_.y != y) {
            // 计算旧的选择区域
            RECT oldRect = {
                std::min(start_.x, cur_.x),
                std::min(start_.y, cur_.y),
                std::max(start_.x, cur_.x),
                std::max(start_.y, cur_.y)
            };
            
            // 更新坐标
            cur_.x = x; 
            cur_.y = y; 
            
            // 计算新的选择区域
            RECT newRect = {
                std::min(start_.x, cur_.x),
                std::min(start_.y, cur_.y),
                std::max(start_.x, cur_.x),
                std::max(start_.y, cur_.y)
            };
            
            // 计算需要重绘的总区域
            RECT unionRect;
            UnionRect(&unionRect, &oldRect, &newRect);
            InflateRect(&unionRect, 20, 20);
            
            // 确保重绘区域在窗口范围内
            RECT clientRect;
            GetClientRect(hwnd_, &clientRect);
            IntersectRect(&unionRect, &unionRect, &clientRect);
            
            // 重绘计算出的区域
            InvalidateRect(hwnd_, &unionRect, FALSE);
        }
    }

    void SelectionOverlay::finishSelect() { 
        if (!selecting_) return; 
        selecting_ = false; 
        
        ReleaseCapture();
        
        RECT selectedRect = RECT{ 
            std::min(start_.x, cur_.x), 
            std::min(start_.y, cur_.y), 
            std::max(start_.x, cur_.x), 
            std::max(start_.y, cur_.y) 
        }; 
        
        // 将窗口坐标转换为屏幕坐标
        RECT windowRect;
        GetWindowRect(hwnd_, &windowRect);
        
        selectedRect.left += windowRect.left;
        selectedRect.top += windowRect.top;
        selectedRect.right += windowRect.left;
        selectedRect.bottom += windowRect.top;
        
        // 存储选择结果
        selectedRect_ = selectedRect;
        notifyOnHide_ = true;
        
        // 立即触发回调
        if (cb_) {
            cb_(selectedRect);
        }
        
        // 开始淡出动画
        startFadeOut();
    }

    void SelectionOverlay::cancelSelection() {
        if (!hwnd_) return;
        
        if (selecting_) {
            ReleaseCapture();
        }
        
        selecting_ = false;
        start_.x = start_.y = 0;
        cur_.x = cur_.y = 0;
        memset(&selectedRect_, 0, sizeof(selectedRect_));
        notifyOnHide_ = false;
        
        startFadeOut();
    }

    bool SelectionOverlay::IsValid() const {
        return hwnd_ != nullptr;
    }

    // ---- 背景图像相关方法实现 ----
    void SelectionOverlay::SetBackgroundImage(const uint8_t* imageData, int width, int height, int stride) {
        if (hwnd_) {
            if (backgroundCheckTimerId_) {
                KillTimer(hwnd_, backgroundCheckTimerId_);
                backgroundCheckTimerId_ = 0;
            }
            
            if (imageData && width > 0 && height > 0) {
                createBackgroundBitmap(imageData, width, height, stride);
                
                // 背景图像加载完成后，如果当前alpha为0且没有在进行动画，启动淡入
                if (!fadingIn_ && !fadingOut_ && alpha_ == 0 && !IsWindowVisible(hwnd_)) {
                    auto style = GetWindowLong(hwnd_, GWL_EXSTYLE);
                    SetWindowLong(hwnd_, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
                    startFadeIn();
                }
                
                // 强制重绘一次，确保背景图像显示
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }

    void SelectionOverlay::createBackgroundBitmap(const uint8_t* imageData, int width, int height, int stride) {
        // 清理旧的背景位图
        destroyBackgroundBitmap();
        
        HDC screenDC = GetDC(nullptr);
        if (!screenDC) return;
        
        // 创建与屏幕兼容的位图
        backgroundBitmap_ = CreateCompatibleBitmap(screenDC, width, height);
        if (!backgroundBitmap_) {
            ReleaseDC(nullptr, screenDC);
            return;
        }
        
        // 创建内存 DC 来操作位图
        HDC memDC = CreateCompatibleDC(screenDC);
        if (!memDC) {
            DeleteObject(backgroundBitmap_);
            backgroundBitmap_ = nullptr;
            ReleaseDC(nullptr, screenDC);
            return;
        }
        
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, backgroundBitmap_);
        
        // 设置位图信息 - 注意：SetDIBits 期望的是 BGR 顺序，而我们的输入是 RGB
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // 负数表示从上到下
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        
        // 检查输入数据格式并转换为 BGR（Windows DIB 格式）
        if (stride == width * 3) {
            // 数据是连续的 RGB，需要转换为 BGR
            std::vector<uint8_t> bgrData(width * height * 3);
            for (int y = 0; y < height; ++y) {
                const uint8_t* srcRow = imageData + y * stride;
                uint8_t* dstRow = bgrData.data() + y * width * 3;
                
                for (int x = 0; x < width; ++x) {
                    dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B <- R
                    dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G <- G  
                    dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R <- B
                }
            }
            SetDIBits(screenDC, backgroundBitmap_, 0, height, bgrData.data(), &bmi, DIB_RGB_COLORS);
        } else {
            // 数据不连续，需要逐行复制并转换为 BGR
            std::vector<uint8_t> bgrData(width * height * 3);
            for (int y = 0; y < height; ++y) {
                const uint8_t* srcRow = imageData + y * stride;
                uint8_t* dstRow = bgrData.data() + y * width * 3;
                
                for (int x = 0; x < width; ++x) {
                    dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B <- R
                    dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G <- G
                    dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R <- B
                }
            }
            SetDIBits(screenDC, backgroundBitmap_, 0, height, bgrData.data(), &bmi, DIB_RGB_COLORS);
        }
        
        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        
        backgroundWidth_ = width;
        backgroundHeight_ = height;
    }

    void SelectionOverlay::destroyBackgroundBitmap() {
        if (backgroundBitmap_) {
            DeleteObject(backgroundBitmap_);
            backgroundBitmap_ = nullptr;
        }
        backgroundWidth_ = 0;
        backgroundHeight_ = 0;
    }
    
    LRESULT CALLBACK SelectionOverlay::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { 
        SelectionOverlay* self = (SelectionOverlay*)GetWindowLongPtr(h, GWLP_USERDATA); 
        if (m == WM_NCCREATE) { 
            CREATESTRUCT* cs = (CREATESTRUCT*)l; 
            SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams); 
            return DefWindowProc(h, m, w, l); 
        }
        if (!self) return DefWindowProc(h, m, w, l); 
        return self->instanceProc(h, m, w, l); 
    }

} // namespace screenshot_tool