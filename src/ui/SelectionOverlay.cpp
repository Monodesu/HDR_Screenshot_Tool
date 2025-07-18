#include "SelectionOverlay.hpp"
#include <algorithm> // 添加算法头文件
#include <vector>    // 添加vector头文件
#include <cmath>     // 添加数学函数头文件
#include "../util/Logger.hpp" // 添加Logger头文件

namespace screenshot_tool {

    static const wchar_t* kOverlayClass = L"ScreenShotOverlayWindow";
    
    // 颜色常量定义 - 统一管理所有暗化相关的颜色
    namespace OverlayColors {
        // 透明键颜色 - 用于分层窗口透明区域
        static constexpr COLORREF TRANSPARENT_KEY = RGB(255, 0, 255);
        
        // 暗化遮罩颜色 - 用于AlphaBlend半透明遮罩效果  
        static constexpr COLORREF DARKEN_MASK = RGB(60, 60, 60);
        
        // 基础暗化颜色 - 用于没有背景图像时的暗化层
        static constexpr COLORREF DARKEN_BASE = RGB(40, 40, 40);
        
        // 回退暗化颜色 - 用于双缓冲失败时的直接绘制
        static constexpr COLORREF DARKEN_FALLBACK = RGB(80, 80, 80);
        
        // UI元素颜色
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
        wc.hbrBackground = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
        RegisterClass(&wc);
        
        // 使用分层窗口，创建时不显示
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 
                               kOverlayClass, L"", WS_POPUP, 
                               0, 0, 0, 0, parent, nullptr, inst, this);
        
        if (hwnd_) {
            // 初始化动画系统
            initializeAnimationSystem();
            
            // 确保窗口创建时就是隐藏状态
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
            fadingIn_ = fadingOut_ = fadingToFullOpaque_ = false;  // 重置所有动画状态
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
        
        // 重置所有动画状态 - 这对于第二次及后续调用很重要
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        if (backgroundCheckTimerId_) {
            KillTimer(hwnd_, backgroundCheckTimerId_);
            backgroundCheckTimerId_ = 0;
        }
        fadingIn_ = fadingOut_ = fadingToFullOpaque_ = false;
        alpha_ = 0;  // 重置透明度
        
        // *** 关键修复1：先隐藏窗口，避免显示旧内容 ***
        ShowWindow(hwnd_, SW_HIDE);
        
        // 清理旧的双缓冲区，将在第一次绘制时重新创建
        destroyBackBuffer();
        
        // *** 关键修复2：清理旧的背景图像，避免显示上一次的冻结帧 ***
        destroyBackgroundBitmap();
        
        // 存储虚拟桌面偏移信息
        virtualDesktopLeft_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtualDesktopTop_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
        
        Logger::Debug(L"Virtual desktop origin: ({}, {})", virtualDesktopLeft_, virtualDesktopTop_);
        Logger::Debug(L"Display rect: ({}, {}) to ({}, {})", 
                     displayRect.left, displayRect.top, displayRect.right, displayRect.bottom);
        
        // 创建字体资源
        createFont();
        
        // 设置窗口位置和大小
        SetWindowPos(hwnd_, HWND_TOPMOST, 
                    displayRect.left, displayRect.top, 
                    displayRect.right - displayRect.left, 
                    displayRect.bottom - displayRect.top, 
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        
        // 设置窗口完全透明，等待背景图像加载
        SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, 255, LWA_COLORKEY);
        
        // *** 关键修复3：不立即显示窗口，等待背景图像加载完成后再显示 ***
        
        // 设置键盘焦点以接收ESC键
        SetFocus(hwnd_);
        
        Logger::Debug(L"Window prepared with initial alpha={}, waiting for background", alpha_);
    }

    // ---- 淡入淡出动画实现 ----
    void SelectionOverlay::startFadeIn() {
        Logger::Debug(L"startFadeIn called, backgroundBitmap_={}", backgroundBitmap_ ? L"valid" : L"null");
        
        // 停止之前的动画和定时器
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
            Logger::Debug(L"Previous timer killed in startFadeIn");
        }
        
        // 强制重置所有动画状态
        fadingIn_ = false;
        fadingOut_ = false;
        fadingToFullOpaque_ = false;
        
        // 启动基于时间的淡入动画
        alpha_ = 0;
        fadingIn_ = true;
        
        // 记录动画开始时间
        QueryPerformanceCounter(&animationStartTime_);
        
        // 窗口始终完全不透明，通过内容渲染控制视觉效果
        SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, 255, LWA_COLORKEY);
        
        // 启动高帧率动画定时器
        timerId_ = SetTimer(hwnd_, FADE_TIMER_ID, fadeInterval_, nullptr);
        if (!timerId_) {
            Logger::Error(L"Failed to create fade timer in startFadeIn");
            // 如果定时器创建失败，直接显示最终效果
            alpha_ = TARGET_ALPHA;
            fadingIn_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        
        // 立即触发重绘显示初始状态
        InvalidateRect(hwnd_, nullptr, FALSE);
        
        Logger::Debug(L"High-FPS fade-in animation started: {}Hz, interval={}ms", 
                     displayRefreshRate_, fadeInterval_);
    }

    void SelectionOverlay::startFadeToFullOpaque() {
        // 这个方法在简化的动画系统中不再使用
        Logger::Debug(L"startFadeToFullOpaque called but not used in simplified mode");
        
        // 确保窗口完全不透明
        alpha_ = TARGET_ALPHA;
        SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, 255, LWA_COLORKEY);
        
        // 触发重绘
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void SelectionOverlay::startFadeOut() {
        Logger::Debug(L"startFadeOut called");
        
        // 停止之前的动画
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        // 启动基于时间的淡出动画
        fadingIn_ = false;
        fadingOut_ = true;
        fadingToFullOpaque_ = false;
        
        // 如果当前alpha为0，直接隐藏
        if (alpha_ <= 0) {
            alpha_ = 0;
            fadingOut_ = false;
            ShowWindow(hwnd_, SW_HIDE);
            Logger::Debug(L"Window hidden immediately (alpha was 0)");
            return;
        }
        
        // 记录动画开始时间
        QueryPerformanceCounter(&animationStartTime_);
        
        // 启动高帧率淡出动画定时器
        timerId_ = SetTimer(hwnd_, FADE_TIMER_ID, fadeInterval_, nullptr);
        if (!timerId_) {
            Logger::Error(L"Failed to create fade timer in startFadeOut");
            // 如果定时器创建失败，直接隐藏
            alpha_ = 0;
            fadingOut_ = false;
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }
        
        Logger::Debug(L"High-FPS fade-out animation started: {}Hz, from alpha={}", 
                     displayRefreshRate_, alpha_);
    }

    void SelectionOverlay::updateFade() {
        bool animationCompleted = false;
        
        // 获取当前动画进度（0.0 到 1.0）
        double progress = getAnimationProgress();
        
        if (fadingIn_) {
            // 淡入：从0到TARGET_ALPHA的平滑过渡
            alpha_ = static_cast<BYTE>(TARGET_ALPHA * progress);
            
            if (progress >= 1.0) {
                alpha_ = TARGET_ALPHA;
                fadingIn_ = false;
                animationCompleted = true;
                Logger::Debug(L"Fade-in completed at alpha={}", alpha_);
            }
        } else if (fadingToFullOpaque_) {
            // 淡入到完全不透明：从当前值到255的平滑过渡
            BYTE startAlpha = (alpha_ < TARGET_ALPHA) ? TARGET_ALPHA : alpha_;
            alpha_ = static_cast<BYTE>(startAlpha + (255 - startAlpha) * progress);
            
            if (progress >= 1.0) {
                alpha_ = 255;
                fadingToFullOpaque_ = false;
                animationCompleted = true;
                Logger::Debug(L"Fade-to-full-opaque completed at alpha={}", alpha_);
            }
        } else if (fadingOut_) {
            // 淡出：从当前alpha到0的平滑过渡
            BYTE startAlpha = alpha_;
            alpha_ = static_cast<BYTE>(startAlpha * (1.0 - progress));
            
            if (progress >= 1.0) {
                alpha_ = 0;
                fadingOut_ = false;
                animationCompleted = true;
                Logger::Debug(L"Fade-out completed at alpha={}", alpha_);
            }
        } else {
            // 错误状态：没有任何动画正在进行，但定时器仍在运行
            Logger::Warn(L"updateFade called but no animation is active! Stopping timer.");
            if (timerId_) {
                KillTimer(hwnd_, timerId_);
                timerId_ = 0;
            }
            return; // 直接返回，不更新窗口
        }
        
        // 触发重绘以更新视觉效果
        InvalidateRect(hwnd_, nullptr, FALSE);
        
        // 如果动画完成，停止定时器并执行完成逻辑
        if (animationCompleted) {
            if (timerId_) {
                KillTimer(hwnd_, timerId_);
                timerId_ = 0;
            }
            
            // 淡出完成时隐藏窗口
            if (alpha_ == 0) {
                ShowWindow(hwnd_, SW_HIDE);
                Logger::Debug(L"Window hidden after fade out complete");
            }
        }
        
        // 性能日志：只在debug模式下记录详细进度
        #ifdef _DEBUG
        static int logCounter = 0;
        if (++logCounter % 10 == 0) {  // 每10帧记录一次，避免日志过多
            Logger::Debug(L"Animation progress: {:.1f}%, alpha={}, {}Hz", 
                         progress * 100.0, alpha_, displayRefreshRate_);
        }
        #endif
    }

    void SelectionOverlay::onFadeComplete() {
        // 这个方法在简化的动画系统中不再使用
        Logger::Debug(L"onFadeComplete called but not used in simplified mode");
        
        // 确保所有动画状态都被重置
        fadingIn_ = false;
        fadingOut_ = false;
        fadingToFullOpaque_ = false;
        
        // 停止任何可能的定时器
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
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
        
        // 根据动画透明度调整渲染效果
        BYTE renderAlpha = alpha_;  // 用于内容渲染的透明度
        
        // 如果有背景图像，显示暗化的背景图像
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
                
                // 将背景图像复制到后台缓冲区
                BitBlt(memDC_, 0, 0, bufferWidth_, bufferHeight_,
                      backgroundDC, offsetX, offsetY, SRCCOPY);
                
                SelectObject(backgroundDC, oldBackgroundBitmap);
                DeleteDC(backgroundDC);
                
                // 根据动画透明度创建渐变的暗化效果
                if (renderAlpha > 0) {
                    HBRUSH darkenBrush = CreateSolidBrush(OverlayColors::DARKEN_MASK);
                    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC_, darkenBrush);
                    
                    // 设置混合模式
                    int oldBkMode = SetBkMode(memDC_, TRANSPARENT);
                    
                    // 根据动画透明度调整暗化强度
                    BLENDFUNCTION blend = {0};
                    blend.BlendOp = AC_SRC_OVER;
                    blend.BlendFlags = 0;
                    blend.SourceConstantAlpha = (BYTE)((120 * renderAlpha) / TARGET_ALPHA); // 动态调整暗化强度
                    blend.AlphaFormat = 0;
                    
                    // 检查是否可以使用AlphaBlend
                    HMODULE hMsimg32 = GetModuleHandleW(L"msimg32.dll");
                    if (!hMsimg32) {
                        hMsimg32 = LoadLibraryW(L"msimg32.dll");
                    }
                    
                    if (hMsimg32) {
                        typedef BOOL (WINAPI *AlphaBlendProc)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
                        AlphaBlendProc pAlphaBlend = (AlphaBlendProc)GetProcAddress(hMsimg32, "AlphaBlend");
                        
                        if (pAlphaBlend && blend.SourceConstantAlpha > 0) {
                            // 创建临时暗化DC
                            HDC darkenDC = CreateCompatibleDC(memDC_);
                            if (darkenDC) {
                                HBITMAP darkenBitmap = CreateCompatibleBitmap(memDC_, bufferWidth_, bufferHeight_);
                                if (darkenBitmap) {
                                    HBITMAP oldDarkenBitmap = (HBITMAP)SelectObject(darkenDC, darkenBitmap);
                                    
                                    // 用统一的暗化颜色填充暗化位图
                                    FillRect(darkenDC, &clientRect, darkenBrush);
                                    
                                    // 应用AlphaBlend暗化
                                    pAlphaBlend(memDC_, 0, 0, bufferWidth_, bufferHeight_,
                                               darkenDC, 0, 0, bufferWidth_, bufferHeight_, blend);
                                    
                                    SelectObject(darkenDC, oldDarkenBitmap);
                                    DeleteObject(darkenBitmap);
                                }
                                DeleteDC(darkenDC);
                            }
                        } else if (renderAlpha > (TARGET_ALPHA / 2)) {
                            // AlphaBlend不可用或透明度过低，在透明度足够时使用简单暗化
                            PatBlt(memDC_, 0, 0, bufferWidth_, bufferHeight_, PATINVERT);
                            FillRect(memDC_, &clientRect, darkenBrush);
                            PatBlt(memDC_, 0, 0, bufferWidth_, bufferHeight_, PATINVERT);
                        }
                    } else if (renderAlpha > (TARGET_ALPHA / 2)) {
                        // 无法加载msimg32.dll，在透明度足够时使用基本暗化
                        FillRect(memDC_, &clientRect, darkenBrush);
                    }
                    
                    SetBkMode(memDC_, oldBkMode);
                    SelectObject(memDC_, oldBrush);
                    DeleteObject(darkenBrush);
                }
                // 如果 renderAlpha == 0，显示原始背景图像（不暗化）
            }
        } else {
            // 没有背景图像时，始终显示透明（等待背景加载）
            HBRUSH clearBrush = CreateSolidBrush(OverlayColors::TRANSPARENT_KEY);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC_, clearBrush);
            FillRect(memDC_, &clientRect, clearBrush);
            SelectObject(memDC_, oldBrush);
            DeleteObject(clearBrush);
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
                HBRUSH clearBrush = CreateSolidBrush(OverlayColors::TRANSPARENT_KEY);
                HBRUSH oldBrush2 = (HBRUSH)SelectObject(memDC_, clearBrush);
                FillRect(memDC_, &selectionRect, clearBrush);
                SelectObject(memDC_, oldBrush2);
                DeleteObject(clearBrush);
            }
            
            // 绘制选择框边框
            HPEN pen = CreatePen(PS_SOLID, 2, OverlayColors::SELECTION_BORDER);
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
        
        // 背景图像设置成功，现在显示窗口并启动淡入动画
        if (hwnd_) {
            Logger::Debug(L"Background image loaded, showing window and starting fade-in animation");
            
            // 先显示窗口
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            
            // 立即重绘，确保显示背景图像
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateWindow(hwnd_);
            
            // 现在启动动画系统
            startFadeIn();
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
                
                // PixelConvert输出的是RGB格式，但Windows位图需要BGR 格式
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
        HBRUSH bgBrush = CreateSolidBrush(OverlayColors::TEXT_BACKGROUND);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, bgBrush);
        
        // 绘制圆角背景矩形
        RoundRect(hdc, textBgRect.left, textBgRect.top, textBgRect.right, textBgRect.bottom, 6, 6);
        
        SelectObject(hdc, oldBrush);
        DeleteObject(bgBrush);
        
        // 设置文本渲染属性
        SetTextColor(hdc, OverlayColors::TEXT_COLOR);
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
        fadingToFullOpaque_ = false;  // 重置所有动画状态
        alpha_ = 255;
        
        // 确保窗口完全可见，只使用颜色键透明度
        SetLayeredWindowAttributes(hwnd_, OverlayColors::TRANSPARENT_KEY, 255, LWA_COLORKEY);
        
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
            
            // 优化：减小边界扩展，从100减少到30，减少重绘面积
            InflateRect(&unionRect, 30, 30);
            
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
                // 如果正在动画且alpha=0，显示透明，否则显示回退颜色
                if ((fadingIn_ && alpha_ == 0) || (fadingOut_ && alpha_ == 0)) {
                    // 动画状态且alpha=0时，使用透明键颜色（完全透明）
                    HBRUSH clearBrush = CreateSolidBrush(OverlayColors::TRANSPARENT_KEY);
                    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, clearBrush);
                    FillRect(dc, &c, clearBrush);
                    SelectObject(dc, oldBrush);
                    DeleteObject(clearBrush);
                } else {
                    // 非动画状态或alpha>0时，使用回退暗化颜色
                    HBRUSH darkBrush = CreateSolidBrush(OverlayColors::DARKEN_FALLBACK);
                    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, darkBrush);
                    FillRect(dc, &c, darkBrush);
                    SelectObject(dc, oldBrush);
                    DeleteObject(darkBrush);
                }
                
                // 只在选择时绘制选择框
                if (selecting_) {
                    RECT selectionRect = {
                        std::min(start_.x, cur_.x),
                        std::min(start_.y, cur_.y),
                        std::max(start_.x, cur_.x),
                        std::max(start_.y, cur_.y)
                    };
                    
                    // 用透明键颜色填充选择区域
                    HBRUSH clearBrush = CreateSolidBrush(OverlayColors::TRANSPARENT_KEY);
                    HBRUSH oldBrush2 = (HBRUSH)SelectObject(dc, clearBrush);
                    FillRect(dc, &selectionRect, clearBrush);
                    SelectObject(dc, oldBrush2);
                    DeleteObject(clearBrush);
                    
                    // 绘制选择框边框
                    HPEN pen = CreatePen(PS_SOLID, 2, OverlayColors::SELECTION_BORDER);
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

    // ---- 高精度基于时间的动画系统实现 ----
    void SelectionOverlay::initializeAnimationSystem() {
        // 获取性能计数器频率（用于高精度时间测量）
        QueryPerformanceFrequency(&performanceFreq_);
        
        // 获取显示器刷新率
        displayRefreshRate_ = getDisplayRefreshRate();
        
        // 计算最优定时器间隔（匹配显示器刷新率）
        fadeInterval_ = 1000 / displayRefreshRate_;  // 毫秒
        
        // 确保间隔不低于1ms（避免过高的CPU占用）
        fadeInterval_ = std::max(1U, fadeInterval_);
        
        Logger::Debug(L"Animation system initialized: refresh={}Hz, interval={}ms", 
                     displayRefreshRate_, fadeInterval_);
    }
    
    UINT SelectionOverlay::getDisplayRefreshRate() {
        if (!hwnd_) return 60;  // 默认60Hz
        
        // 获取窗口所在的显示器
        HMONITOR hMonitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        
        // 获取显示器信息
        MONITORINFOEX monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        
        if (GetMonitorInfo(hMonitor, &monitorInfo)) {
            // 获取显示设备的显示模式
            DEVMODE devMode = {};
            devMode.dmSize = sizeof(DEVMODE);
            
            if (EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode)) {
                if (devMode.dmDisplayFrequency > 0 && devMode.dmDisplayFrequency <= 500) {
                    Logger::Debug(L"Detected display refresh rate: {}Hz", devMode.dmDisplayFrequency);
                    return devMode.dmDisplayFrequency;
                }
            }
        }
        
        Logger::Debug(L"Failed to detect refresh rate, using default 60Hz");
        return 60;  // 回退到60Hz
    }
    
    double SelectionOverlay::getAnimationProgress() {
        if (performanceFreq_.QuadPart == 0) return 1.0;  // 如果未初始化，返回完成状态
        
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        
        // 计算经过的时间（秒）
        double elapsedSeconds = static_cast<double>(currentTime.QuadPart - animationStartTime_.QuadPart) 
                               / static_cast<double>(performanceFreq_.QuadPart);
        
        // 计算进度（0.0 到 1.0）
        double progress = elapsedSeconds / FADE_DURATION;
        
        // 确保进度在有效范围内
        return std::min(1.0, std::max(0.0, progress));
    }

} // namespace screenshot_tool