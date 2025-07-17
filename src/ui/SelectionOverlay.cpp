#include "SelectionOverlay.hpp"
#include <algorithm> // 添加算法头文件
#include <vector>    // 添加vector头文件
#include "../util/Logger.hpp" // 添加Logger头文件

namespace screenshot_tool {

    static const wchar_t* kOverlayClass = L"ScreenShotOverlayWindow";

    bool SelectionOverlay::Create(HINSTANCE inst, HWND parent, Callback cb) {
        parent_ = parent; 
        cb_ = std::move(cb);
        
        WNDCLASS wc{};
        wc.lpfnWndProc = SelectionOverlay::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = kOverlayClass;
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        wc.hbrBackground = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
        RegisterClass(&wc);
        
        // 使用分层窗口，移除WS_EX_NOACTIVATE以允许接收键盘输入
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 
                               kOverlayClass, L"", WS_POPUP, 
                               0, 0, 0, 0, parent, nullptr, inst, this);
        return hwnd_ != nullptr;
    }

    SelectionOverlay::~SelectionOverlay() {
        // 清理定时器
        if (timerId_ && hwnd_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        // 清理背景检测定时器
        if (backgroundCheckTimerId_ && hwnd_) {
            KillTimer(hwnd_, backgroundCheckTimerId_);
            backgroundCheckTimerId_ = 0;
        }
        
        // 清理双缓冲资源
        destroyBackBuffer();
        
        // 清理背景图像资源
        destroyBackgroundBitmap();
        
        // 清理字体资源
        destroyFont();
        
        // 销毁窗口
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void SelectionOverlay::Hide() {
        if (!hwnd_) return;
        
        // 停止背景检测定时器
        if (backgroundCheckTimerId_) {
            KillTimer(hwnd_, backgroundCheckTimerId_);
            backgroundCheckTimerId_ = 0;
        }
        
        // 如果正在选择，直接隐藏；否则开始淡出动画
        if (selecting_) {
            // 停止任何进行中的动画
            if (timerId_) {
                KillTimer(hwnd_, timerId_);
                timerId_ = 0;
            }
            fadingIn_ = fadingOut_ = false;
            ShowWindow(hwnd_, SW_HIDE);
        } else {
            startFadeOut();
        }
    }

    bool SelectionOverlay::IsValid() const {
        return hwnd_ != nullptr;
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
    }

    void SelectionOverlay::BeginSelectOnMonitor(const RECT& monitorRect) {
        useMonitorConstraint_ = true;
        monitorConstraint_ = monitorRect;
        ShowWithRect(monitorRect);
    }

    void SelectionOverlay::ShowWithRect(const RECT& displayRect) {
        if (!hwnd_) return; 
        
        // 清除之前的选择状态
        selecting_ = false;
        start_.x = start_.y = 0;
        cur_.x = cur_.y = 0;
        memset(&selectedRect_, 0, sizeof(selectedRect_));
        notifyOnHide_ = false;
        
        // 存储虚拟桌面偏移信息
        virtualDesktopLeft_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtualDesktopTop_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
        
        Logger::Debug(L"Virtual desktop origin: ({}, {})", virtualDesktopLeft_, virtualDesktopTop_);
        Logger::Debug(L"Display rect: ({}, {}) to ({}, {})", 
                     displayRect.left, displayRect.top, displayRect.right, displayRect.bottom);
        
        // 设置窗口覆盖指定区域
        SetWindowPos(hwnd_, HWND_TOPMOST, 
                    displayRect.left, displayRect.top, 
                    displayRect.right - displayRect.left, 
                    displayRect.bottom - displayRect.top, 
                    SWP_SHOWWINDOW);
        
        // 清理旧的双缓冲区，将在第一次绘制时重新创建
        destroyBackBuffer();
        
        // 创建字体资源
        createFont();
        
        // 设置键盘焦点以接收ESC键
        SetFocus(hwnd_);
        
        // 开始淡入动画
        startFadeIn();
        
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    // ---- 淡入淡出动画实现 ----
    void SelectionOverlay::startFadeIn() {
        Logger::Debug(L"startFadeIn called, backgroundBitmap_={}", backgroundBitmap_ ? L"valid" : L"null");
        
        // 无论是否有背景图像，都使用淡入效果
        // 停止之前的动画
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        // 初始化淡入状态
        alpha_ = 0;
        fadingIn_ = true;
        fadingOut_ = false;
        
        // 使用颜色键透明度 - 品红色(RGB(255,0,255))将被视为透明
        // 同时设置alpha透明度用于淡入淡出效果
        SetLayeredWindowAttributes(hwnd_, RGB(255, 0, 255), alpha_, LWA_COLORKEY | LWA_ALPHA);
        
        // 启动定时器
        timerId_ = SetTimer(hwnd_, FADE_TIMER_ID, FADE_INTERVAL, nullptr);
        
        Logger::Debug(L"Fade-in animation started with alpha={}", alpha_);
    }

    void SelectionOverlay::startFadeOut() {
        // 停止之前的动画
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        // 初始化淡出状态
        fadingIn_ = false;
        fadingOut_ = true;
        
        // 启动定时器
        timerId_ = SetTimer(hwnd_, FADE_TIMER_ID, FADE_INTERVAL, nullptr);
    }

    void SelectionOverlay::updateFade() {
        if (fadingIn_) {
            alpha_ += FADE_STEP;
            if (alpha_ >= TARGET_ALPHA) {
                alpha_ = TARGET_ALPHA;
                fadingIn_ = false;
                onFadeComplete();
            }
        } else if (fadingOut_) {
            if (alpha_ <= FADE_STEP) {
                alpha_ = 0;
                fadingOut_ = false;
                onFadeComplete();
            } else {
                alpha_ -= FADE_STEP;
            }
        }
        
        // 更新窗口透明度 - 继续使用颜色键和alpha组合
        SetLayeredWindowAttributes(hwnd_, RGB(255, 0, 255), alpha_, LWA_COLORKEY | LWA_ALPHA);
    }

    void SelectionOverlay::onFadeComplete() {
        // 停止定时器
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        // 如果淡出完成，隐藏窗口
        if (!fadingIn_ && !fadingOut_ && alpha_ == 0) {
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    // ---- 双缓冲绘制实现 ----
    void SelectionOverlay::createBackBuffer(int width, int height) {
        // 如果尺寸没有变化，不需要重新创建
        if (memDC_ && bufferWidth_ == width && bufferHeight_ == height) {
            return;
        }
        
        // 清理旧的缓冲区
        destroyBackBuffer();
        
        // 创建新的双缓冲区
        HDC hdc = GetDC(hwnd_);
        if (hdc) {
            memDC_ = CreateCompatibleDC(hdc);
            if (memDC_) {
                memBitmap_ = CreateCompatibleBitmap(hdc, width, height);
                if (memBitmap_) {
                    oldBitmap_ = (HBITMAP)SelectObject(memDC_, memBitmap_);
                    bufferWidth_ = width;
                    bufferHeight_ = height;
                }
            }
            ReleaseDC(hwnd_, hdc);
        }
    }

    void SelectionOverlay::destroyBackBuffer() {
        if (memDC_) {
            if (oldBitmap_) {
                SelectObject(memDC_, oldBitmap_);
                oldBitmap_ = nullptr;
            }
            DeleteDC(memDC_);
            memDC_ = nullptr;
        }
        
        if (memBitmap_) {
            DeleteObject(memBitmap_);
            memBitmap_ = nullptr;
        }
        
        bufferWidth_ = bufferHeight_ = 0;
    }

    void SelectionOverlay::renderToBackBuffer() {
        if (!memDC_) return;
        
        RECT clientRect = { 0, 0, bufferWidth_, bufferHeight_ };
        
        // 定义透明区域的颜色键 - 使用纯品红色作为透明键
        static const COLORREF TRANSPARENT_KEY = RGB(255, 0, 255);
        // 定义暗化颜色 - 深灰色
        static const COLORREF DARKEN_COLOR = RGB(40, 40, 40);
        
        // 如果有背景图像，先显示背景
        if (backgroundBitmap_) {
            // 创建临时DC来处理背景图像
            HDC backgroundDC = CreateCompatibleDC(memDC_);
            if (backgroundDC) {
                HBITMAP oldBackgroundBitmap = (HBITMAP)SelectObject(backgroundDC, backgroundBitmap_);
                
                // 获取窗口在虚拟桌面中的位置
                RECT windowRect;
                GetWindowRect(hwnd_, &windowRect);
                
                // 使用存储的虚拟桌面偏移信息
                int offsetX = windowRect.left - virtualDesktopLeft_;
                int offsetY = windowRect.top - virtualDesktopTop_;
                
                Logger::Debug(L"Window rect: ({}, {}) to ({}, {})", windowRect.left, windowRect.top, windowRect.right, windowRect.bottom);
                Logger::Debug(L"Virtual desktop offset: ({}, {})", virtualDesktopLeft_, virtualDesktopTop_);
                Logger::Debug(L"Background offset: ({}, {})", offsetX, offsetY);
                
                // 将背景图像复制到后台缓冲区
                BitBlt(memDC_, 0, 0, bufferWidth_, bufferHeight_,
                      backgroundDC, offsetX, offsetY, SRCCOPY);
                
                SelectObject(backgroundDC, oldBackgroundBitmap);
                DeleteDC(backgroundDC);
                
                // 使用简单的混合方式创建暗化效果
                // 创建带有alpha通道的黑色层，实现暗化效果
                HDC darkDC = CreateCompatibleDC(memDC_);
                if (darkDC) {
                    HBITMAP darkBitmap = CreateCompatibleBitmap(memDC_, bufferWidth_, bufferHeight_);
                    if (darkBitmap) {
                        HBITMAP oldDarkBitmap = (HBITMAP)SelectObject(darkDC, darkBitmap);
                        
                        // 创建半透明暗化效果
                        HBRUSH darkBrush = CreateSolidBrush(RGB(0, 0, 0));
                        HBRUSH oldBrush = (HBRUSH)SelectObject(darkDC, darkBrush);
                        FillRect(darkDC, &clientRect, darkBrush);
                        SelectObject(darkDC, oldBrush);
                        DeleteObject(darkBrush);
                        
                        // 使用SRCPAINT模式创建暗化效果
                        // 这会让原图像变暗但不会变成纯黑色
                        BLENDFUNCTION bf = {};
                        bf.BlendOp = AC_SRC_OVER;
                        bf.BlendFlags = 0;
                        bf.SourceConstantAlpha = 100; // 调整暗化程度
                        bf.AlphaFormat = 0;
                        
                        // 使用Windows API的PatBlt创建简单暗化
                        COLORREF oldBkColor = SetBkColor(memDC_, RGB(0, 0, 0));
                        COLORREF oldTextColor = SetTextColor(memDC_, RGB(128, 128, 128));
                        PatBlt(memDC_, 0, 0, bufferWidth_, bufferHeight_, PATINVERT);
                        PatBlt(memDC_, 0, 0, bufferWidth_, bufferHeight_, PATINVERT);
                        SetBkColor(memDC_, oldBkColor);
                        SetTextColor(memDC_, oldTextColor);
                        
                        SelectObject(darkDC, oldDarkBitmap);
                        DeleteObject(darkBitmap);
                    }
                    DeleteDC(darkDC);
                }
            }
        } else {
            // 没有背景图像时，使用纯暗化层
            HBRUSH darkBrush = CreateSolidBrush(DARKEN_COLOR);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC_, darkBrush);
            FillRect(memDC_, &clientRect, darkBrush);
            SelectObject(memDC_, oldBrush);
            DeleteObject(darkBrush);
        }
        
        // 如果正在选择，在选择区域恢复原始背景（清除暗化效果）
        if (selecting_) {
            // 计算选择区域
            RECT selectionRect = {
                std::min(start_.x, cur_.x),
                std::min(start_.y, cur_.y),
                std::max(start_.x, cur_.x),
                std::max(start_.y, cur_.y)
            };
            
            // 如果有背景图像，在选择区域恢复原始背景（清除暗化效果）
            if (backgroundBitmap_) {
                HDC backgroundDC = CreateCompatibleDC(memDC_);
                if (backgroundDC) {
                    HBITMAP oldBackgroundBitmap = (HBITMAP)SelectObject(backgroundDC, backgroundBitmap_);
                    
                    // 获取窗口在虚拟桌面中的位置
                    RECT windowRect;
                    GetWindowRect(hwnd_, &windowRect);
                    
                    // 使用存储的虚拟桌面偏移信息
                    int offsetX = windowRect.left - virtualDesktopLeft_;
                    int offsetY = windowRect.top - virtualDesktopTop_;
                    
                    // 在选择区域恢复原始背景（清除暗化效果）
                    BitBlt(memDC_, selectionRect.left, selectionRect.top,
                          selectionRect.right - selectionRect.left,
                          selectionRect.bottom - selectionRect.top,
                          backgroundDC, 
                          offsetX + selectionRect.left, 
                          offsetY + selectionRect.top, 
                          SRCCOPY);
                    
                    SelectObject(backgroundDC, oldBackgroundBitmap);
                    DeleteDC(backgroundDC);
                }
            } else {
                // 没有背景图像时，使用透明键颜色填充选择区域
                HBRUSH clearBrush = CreateSolidBrush(TRANSPARENT_KEY);
                HBRUSH oldBrush2 = (HBRUSH)SelectObject(memDC_, clearBrush);
                FillRect(memDC_, &selectionRect, clearBrush);
                SelectObject(memDC_, oldBrush2);
                DeleteObject(clearBrush);
            }
            
            // 绘制选择框边框
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HPEN oldPen = (HPEN)SelectObject(memDC_, pen);
            SetBkMode(memDC_, TRANSPARENT);
            
            MoveToEx(memDC_, selectionRect.left, selectionRect.top, nullptr);
            LineTo(memDC_, selectionRect.right, selectionRect.top);
            LineTo(memDC_, selectionRect.right, selectionRect.bottom);
            LineTo(memDC_, selectionRect.left, selectionRect.bottom);
            LineTo(memDC_, selectionRect.left, selectionRect.top);
            
            SelectObject(memDC_, oldPen);
            DeleteObject(pen);
            
            // 显示尺寸信息
            drawSizeText(memDC_, selectionRect);
        }
    }

    void SelectionOverlay::createMaskForSelection() {
        // 这个方法可以用于更复杂的遮罩创建，目前在renderToBackBuffer中直接处理
    }
    
    // ---- 背景图像相关方法实现 ----
    void SelectionOverlay::SetBackgroundImage(const uint8_t* imageData, int width, int height, int stride) {
        if (!imageData || width <= 0 || height <= 0) {
            Logger::Debug(L"SetBackgroundImage: clearing background (null data or invalid size)");
            destroyBackgroundBitmap();
            return;
        }
        
        Logger::Debug(L"SetBackgroundImage: setting background {}x{}, stride={}", width, height, stride);
        createBackgroundBitmap(imageData, width, height, stride);
        
        // 停止背景检测定时器，因为背景已经设置成功
        if (backgroundCheckTimerId_) {
            KillTimer(hwnd_, backgroundCheckTimerId_);
            backgroundCheckTimerId_ = 0;
        }
        
        // 如果窗口已经显示，立即重绘显示背景图像
        if (hwnd_ && IsWindowVisible(hwnd_)) {
            Logger::Debug(L"Window is visible, updating display immediately with background");
            
            // 强制重绘以显示背景图像
            InvalidateRect(hwnd_, nullptr, TRUE);
            UpdateWindow(hwnd_);
        }
    }
    
    void SelectionOverlay::StartWaitingForBackground(std::function<void()> backgroundCheckCallback) {
        backgroundCheckCallback_ = std::move(backgroundCheckCallback);
        
        // 启动背景检测定时器
        if (hwnd_) {
            backgroundCheckTimerId_ = SetTimer(hwnd_, BACKGROUND_CHECK_TIMER_ID, BACKGROUND_CHECK_INTERVAL, nullptr);
            Logger::Debug(L"Started background check timer");
        }
    }

    void SelectionOverlay::createBackgroundBitmap(const uint8_t* imageData, int width, int height, int stride) {
        // 清理旧的背景图像
        destroyBackgroundBitmap();
        
        if (!hwnd_ || !imageData) return;
        
        HDC hdc = GetDC(hwnd_);
        if (!hdc) return;
        
        // 创建设备兼容的位图
        backgroundBitmap_ = CreateCompatibleBitmap(hdc, width, height);
        if (backgroundBitmap_) {
            HDC memDC = CreateCompatibleDC(hdc);
            if (memDC) {
                HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, backgroundBitmap_);
                
                // 创建位图信息结构 - PixelConvert已经输出RGB格式，但Windows期望BGR
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = width;
                bmi.bmiHeader.biHeight = -height; // 负值表示自上而下的位图
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 24; // 24位RGB格式
                bmi.bmiHeader.biCompression = BI_RGB;
                
                // PixelConvert输出的是RGB格式，但Windows位图需要BGR格式
                // 需要转换RGB到BGR
                std::vector<uint8_t> bgrData(width * height * 3);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        int srcIndex = y * stride + x * 3;
                        int dstIndex = y * width * 3 + x * 3;
                        
                        // 边界检查
                        if (srcIndex + 2 < static_cast<int>(stride * height) && 
                            dstIndex + 2 < static_cast<int>(bgrData.size())) {
                            // RGB -> BGR转换
                            bgrData[dstIndex + 0] = imageData[srcIndex + 2]; // B
                            bgrData[dstIndex + 1] = imageData[srcIndex + 1]; // G  
                            bgrData[dstIndex + 2] = imageData[srcIndex + 0]; // R
                        }
                    }
                }
                
                // 将转换后的图像数据写入位图
                int result = SetDIBits(memDC, backgroundBitmap_, 0, height, bgrData.data(), &bmi, DIB_RGB_COLORS);
                if (result == 0) {
                    Logger::Warn(L"SetDIBits failed for background bitmap");
                } else {
                    Logger::Debug(L"Successfully created background bitmap: {}x{}", width, height);
                }
                
                backgroundWidth_ = width;
                backgroundHeight_ = height;
                
                SelectObject(memDC, oldBitmap);
                DeleteDC(memDC);
            } else {
                DeleteObject(backgroundBitmap_);
                backgroundBitmap_ = nullptr;
                Logger::Error(L"Failed to create compatible DC for background bitmap");
            }
        } else {
            Logger::Error(L"Failed to create compatible bitmap");
        }
        
        ReleaseDC(hwnd_, hdc);
    }
    
    void SelectionOverlay::destroyBackgroundBitmap() {
        if (backgroundBitmap_) {
            DeleteObject(backgroundBitmap_);
            backgroundBitmap_ = nullptr;
        }
        backgroundWidth_ = backgroundHeight_ = 0;
    }

    // ---- 字体相关方法实现 ----
    void SelectionOverlay::createFont() {
        // 如果字体已存在，先清理
        destroyFont();
        
        // 创建高质量的字体用于显示尺寸信息
        sizeFont_ = CreateFontW(
            -16,                    // 字体高度
            0,                      // 字体宽度（0表示自动计算）
            0,                      // 文字输出角度
            0,                      // 字体倾斜角度
            FW_NORMAL,              // 字体粗细
            FALSE,                  // 是否斜体
            FALSE,                  // 是否下划线
            FALSE,                  // 是否删除线
            DEFAULT_CHARSET,        // 字符集
            OUT_TT_PRECIS,          // 输出精度
            CLIP_DEFAULT_PRECIS,    // 裁剪精度
            CLEARTYPE_QUALITY,      // 字体质量（ClearType抗锯齿）
            DEFAULT_PITCH | FF_SWISS, // 字体族
            L"Segoe UI"             // 字体名称
        );
        
        // 如果Segoe UI不可用，使用备用字体
        if (!sizeFont_) {
            sizeFont_ = CreateFontW(
                -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial"
            );
        }
        
        // 如果还是失败，使用系统默认字体
        if (!sizeFont_) {
            sizeFont_ = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        }
    }
    
    void SelectionOverlay::destroyFont() {
        if (sizeFont_ && sizeFont_ != GetStockObject(DEFAULT_GUI_FONT)) {
            DeleteObject(sizeFont_);
            sizeFont_ = nullptr;
        }
    }
    
    void SelectionOverlay::drawSizeText(HDC hdc, const RECT& selectionRect) {
        if (!sizeFont_) return;
        
        int width = abs(cur_.x - start_.x);
        int height = abs(cur_.y - start_.y);
        
        // 格式化尺寸文本为 "1920 × 1080" 格式
        wchar_t sizeText[64];
        swprintf_s(sizeText, L"%d × %d", width, height);
        
        // 选择字体
        HFONT oldFont = (HFONT)SelectObject(hdc, sizeFont_);
        
        // 计算文本尺寸
        SIZE textSize;
        GetTextExtentPoint32W(hdc, sizeText, (int)wcslen(sizeText), &textSize);
        
        // 计算文本位置
        int textX = std::min(start_.x, cur_.x);
        int textY = std::min(start_.y, cur_.y) - textSize.cy - 8;
        
        // 如果文本会显示在顶部之外，则显示在选择框内部
        if (textY < 5) {
            textY = std::min(start_.y, cur_.y) + 8;
        }
        
        // 确保文本不会超出左边界
        if (textX < 5) textX = 5;
        
        // 创建文本背景区域
        RECT textBgRect = {
            textX - 6, textY - 3,
            textX + textSize.cx + 6, textY + textSize.cy + 3
        };
        
        // 绘制半透明的文本背景
        HBRUSH bgBrush = CreateSolidBrush(RGB(40, 40, 40));
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, bgBrush);
        
        // 绘制圆角背景矩形
        RoundRect(hdc, textBgRect.left, textBgRect.top, textBgRect.right, textBgRect.bottom, 6, 6);
        
        SelectObject(hdc, oldBrush);
        DeleteObject(bgBrush);
        
        // 设置文本渲染属性
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        
        // 绘制文本
        TextOutW(hdc, textX, textY, sizeText, (int)wcslen(sizeText));
        
        // 恢复原字体
        SelectObject(hdc, oldFont);
    }

    void SelectionOverlay::startSelect(int x, int y) { 
        selecting_ = true; 
        start_.x = x; 
        start_.y = y; 
        cur_ = start_; 
        
        // 捕获鼠标以确保在缩小区域时不会丢失追踪
        SetCapture(hwnd_);
        
        // 开始选择时，立即停止任何动画并确保窗口完全可见
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        fadingIn_ = false;
        fadingOut_ = false;
        alpha_ = 255;
        
        // 确保窗口完全可见，只使用颜色键透明度
        SetLayeredWindowAttributes(hwnd_, RGB(255, 0, 255), 255, LWA_COLORKEY);
        
        InvalidateRect(hwnd_, nullptr, FALSE); 
    }
    
    void SelectionOverlay::updateSelect(int x, int y) { 
        // 如果使用显示器约束，限制坐标在约束范围内
        if (useMonitorConstraint_) {
            RECT windowRect;
            GetWindowRect(hwnd_, &windowRect);
            
            // 将窗口坐标转换为屏幕坐标
            int screenX = x + windowRect.left;
            int screenY = y + windowRect.top;
            
            // 限制在约束范围内
            screenX = std::max(static_cast<int>(monitorConstraint_.left), 
                              std::min(screenX, static_cast<int>(monitorConstraint_.right) - 1));
            screenY = std::max(static_cast<int>(monitorConstraint_.top), 
                              std::min(screenY, static_cast<int>(monitorConstraint_.bottom) - 1));
            
            // 转换回窗口坐标
            x = screenX - windowRect.left;
            y = screenY - windowRect.top;
        }
        
        // 只在坐标真正变化时才重绘 - 这对性能很重要
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
            
            // 计算需要重绘的总区域（包括旧区域和新区域）
            RECT unionRect;
            UnionRect(&unionRect, &oldRect, &newRect);
            
            // 扩展边界以确保完全覆盖边框和文字
            InflateRect(&unionRect, 100, 100);
            
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
        
        // 释放鼠标捕获
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
        
        // 立即触发回调
        if (cb_) {
            cb_(selectedRect);
        }
        
        // 开始淡出动画
        startFadeOut();
    }

    void SelectionOverlay::cancelSelection() {
        if (!hwnd_) return;
        
        // 如果正在选择，释放鼠标捕获
        if (selecting_) {
            ReleaseCapture();
        }
        
        // 取消选择状态
        selecting_ = false;
        start_.x = start_.y = 0;
        cur_.x = cur_.y = 0;
        memset(&selectedRect_, 0, sizeof(selectedRect_));
        notifyOnHide_ = false;
        
        // 开始淡出动画
        startFadeOut();
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
                // 背景检测定时器
                if (backgroundCheckCallback_) {
                    backgroundCheckCallback_();
                }
            }
            return 0;
            
        case WM_PAINT: { 
            PAINTSTRUCT ps; 
            HDC dc = BeginPaint(h, &ps); 
            RECT c; 
            GetClientRect(h, &c); 
            
            // 创建或更新双缓冲区
            int width = c.right - c.left;
            int height = c.bottom - c.top;
            createBackBuffer(width, height);
            
            if (memDC_) {
                // 渲染到后台缓冲区
                renderToBackBuffer();
                
                // 将后台缓冲区内容复制到前台
                BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
                      ps.rcPaint.right - ps.rcPaint.left,
                      ps.rcPaint.bottom - ps.rcPaint.top,
                      memDC_, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            } else {
                // 双缓冲创建失败，回退到直接绘制
                // 创建暗化层作为基础
                HBRUSH darkBrush = CreateSolidBrush(RGB(20, 20, 20));
                HBRUSH oldBrush = (HBRUSH)SelectObject(dc, darkBrush);
                FillRect(dc, &c, darkBrush);
                SelectObject(dc, oldBrush);
                DeleteObject(darkBrush);
                
                // 只在选择时绘制选择框
                if (selecting_) {
                    RECT selectionRect = {
                        std::min(start_.x, cur_.x),
                        std::min(start_.y, cur_.y),
                        std::max(start_.x, cur_.x),
                        std::max(start_.y, cur_.y)
                    };
                    
                    // 用透明键颜色填充选择区域
                    HBRUSH clearBrush = CreateSolidBrush(RGB(255, 0, 255));
                    HBRUSH oldBrush2 = (HBRUSH)SelectObject(dc, clearBrush);
                    FillRect(dc, &selectionRect, clearBrush);
                    SelectObject(dc, oldBrush2);
                    DeleteObject(clearBrush);
                    
                    // 绘制选择框边框
                    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                    HPEN oldPen = (HPEN)SelectObject(dc, pen);
                    SetBkMode(dc, TRANSPARENT);
                    
                    MoveToEx(dc, selectionRect.left, selectionRect.top, nullptr);
                    LineTo(dc, selectionRect.right, selectionRect.top);
                    LineTo(dc, selectionRect.right, selectionRect.bottom);
                    LineTo(dc, selectionRect.left, selectionRect.bottom);
                    LineTo(dc, selectionRect.left, selectionRect.top);
                    
                    SelectObject(dc, oldPen);
                    DeleteObject(pen);
                    
                    // 显示尺寸信息
                    drawSizeText(dc, selectionRect);
                }
            }
            
            EndPaint(h, &ps); 
            return 0;
        }
        
        default:
            return DefWindowProc(h, m, w, l);
        }
    }

} // namespace screenshot_tool