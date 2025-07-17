#include "SelectionOverlay.hpp"

namespace screenshot_tool {

    static const wchar_t* kOverlayClass = L"ScreenShotOverlayWindow";

    bool SelectionOverlay::Create(HINSTANCE inst, HWND parent, Callback cb) {
        parent_ = parent; cb_ = std::move(cb);
        WNDCLASS wc{}; wc.lpfnWndProc = SelectionOverlay::WndProc; wc.hInstance = inst; wc.lpszClassName = kOverlayClass; wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        wc.hbrBackground = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
        RegisterClass(&wc);
        // 使用分层窗口
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kOverlayClass, L"", WS_POPUP, 0, 0, 0, 0, parent, nullptr, inst, this);
        return hwnd_ != nullptr;
    }

    SelectionOverlay::~SelectionOverlay() {
        // 清理定时器
        if (timerId_ && hwnd_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        // 清理双缓冲资源
        destroyBackBuffer();
        
        // 销毁窗口
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void SelectionOverlay::Show() {
        if (!hwnd_) return; 
        
        // 清除之前的选择状态
        selecting_ = false;
        start_ = {};
        cur_ = {};
        selectedRect_ = {};
        notifyOnHide_ = false;
        
        RECT r; 
        if (useMonitorConstraint_) {
            // 使用显示器约束
            r = monitorConstraint_;
        } else {
            // 使用整个虚拟桌面，支持跨显示器选择
            r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }
        
        SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_SHOWWINDOW);
        
        // 清理旧的双缓冲区，将在第一次绘制时重新创建
        destroyBackBuffer();
        
        // 开始淡入动画
        startFadeIn();
        
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void SelectionOverlay::Hide() {
        if (!hwnd_) return;
        
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
        useMonitorConstraint_ = false;
        Show();
    }

    void SelectionOverlay::BeginSelectOnMonitor(const RECT& monitorRect) {
        useMonitorConstraint_ = true;
        monitorConstraint_ = monitorRect;
        ShowOnMonitor(monitorRect);
    }

    void SelectionOverlay::ShowOnMonitor(const RECT& monitorRect) {
        if (!hwnd_) return; 
        
        // 清除之前的选择状态
        selecting_ = false;
        start_ = {};
        cur_ = {};
        selectedRect_ = {};
        notifyOnHide_ = false;
        
        // 设置窗口大小为指定显示器大小
        SetWindowPos(hwnd_, HWND_TOPMOST, 
                    monitorRect.left, monitorRect.top, 
                    monitorRect.right - monitorRect.left, 
                    monitorRect.bottom - monitorRect.top, 
                    SWP_SHOWWINDOW);
        
        // 清理旧的双缓冲区，将在第一次绘制时重新创建
        destroyBackBuffer();
        
        // 开始淡入动画
        startFadeIn();
        
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    // ---- 淡入淡出动画实现 ----
    void SelectionOverlay::startFadeIn() {
        // 停止之前的动画
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        
        // 初始化淡入状态
        alpha_ = 0;
        fadingIn_ = true;
        fadingOut_ = false;
        
        // 设置初始透明度
        SetLayeredWindowAttributes(hwnd_, 0, alpha_, LWA_ALPHA);
        
        // 启动定时器
        timerId_ = SetTimer(hwnd_, FADE_TIMER_ID, FADE_INTERVAL, nullptr);
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
        
        // 更新窗口透明度
        SetLayeredWindowAttributes(hwnd_, 0, alpha_, LWA_ALPHA);
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
        
        // 绘制暗化背景
        HBRUSH darkBrush = CreateSolidBrush(RGB(0, 0, 0));
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC_, darkBrush);
        FillRect(memDC_, &clientRect, darkBrush);
        SelectObject(memDC_, oldBrush);
        DeleteObject(darkBrush);
        
        // 只在选择时绘制选择框
        if (selecting_) {
            // 绘制选择框
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HPEN oldPen = (HPEN)SelectObject(memDC_, pen);
            SetBkMode(memDC_, TRANSPARENT);
            
            MoveToEx(memDC_, start_.x, start_.y, nullptr);
            LineTo(memDC_, cur_.x, start_.y);
            LineTo(memDC_, cur_.x, cur_.y);
            LineTo(memDC_, start_.x, cur_.y);
            LineTo(memDC_, start_.x, start_.y);
            
            SelectObject(memDC_, oldPen);
            DeleteObject(pen);
            
            // 显示尺寸信息
            SetTextColor(memDC_, RGB(255, 255, 255));
            SetBkColor(memDC_, RGB(0, 0, 0));
            SetBkMode(memDC_, OPAQUE);
            
            int width = abs(cur_.x - start_.x);
            int height = abs(cur_.y - start_.y);
            wchar_t sizeText[32];
            swprintf_s(sizeText, L"%d×%d", width, height);
            
            int textX = std::min(start_.x, cur_.x);
            int textY = std::min(start_.y, cur_.y) - 25;
            if (textY < 0) textY = std::min(start_.y, cur_.y) + 5;
            
            TextOut(memDC_, textX, textY, sizeText, (int)wcslen(sizeText));
        }
    }

    void SelectionOverlay::startSelect(int x, int y) { 
        selecting_ = true; 
        start_.x = x; 
        start_.y = y; 
        cur_ = start_; 
        
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
            screenX = std::max((int)monitorConstraint_.left, std::min(screenX, (int)monitorConstraint_.right - 1));
            screenY = std::max((int)monitorConstraint_.top, std::min(screenY, (int)monitorConstraint_.bottom - 1));
            
            // 转换回窗口坐标
            x = screenX - windowRect.left;
            y = screenY - windowRect.top;
        }
        
        // 只有当坐标实际发生变化时才进行重绘
        if (cur_.x != x || cur_.y != y) {
            // 计算需要重绘的区域（包括旧的和新的选择框）
            RECT oldRect = {
                std::min(start_.x, cur_.x),
                std::min(start_.y, cur_.y),
                std::max(start_.x, cur_.x),
                std::max(start_.y, cur_.y)
            };
            
            cur_.x = x; 
            cur_.y = y; 
            
            RECT newRect = {
                std::min(start_.x, cur_.x),
                std::min(start_.y, cur_.y),
                std::max(start_.x, cur_.x),
                std::max(start_.y, cur_.y)
            };
            
            // 合并两个区域，只重绘变化的部分
            RECT updateRect;
            UnionRect(&updateRect, &oldRect, &newRect);
            
            // 扩展一点边界以确保完全覆盖线条和文字
            InflateRect(&updateRect, 50, 50);
            
            // 只重绘变化的区域
            InvalidateRect(hwnd_, &updateRect, FALSE);
        }
    }
    
    void SelectionOverlay::finishSelect() { 
        if (!selecting_) return; 
        selecting_ = false; 
        
        RECT selectedRect = RECT{ 
            std::min(start_.x, cur_.x), 
            std::min(start_.y, cur_.y), 
            std::max(start_.x, cur_.x), 
            std::max(start_.y, cur_.y) 
        }; 
        
        // 将窗口坐标转换为屏幕坐标
        // 无论是否使用显示器约束，都需要进行坐标转换
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

    LRESULT CALLBACK SelectionOverlay::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { SelectionOverlay* self = (SelectionOverlay*)GetWindowLongPtr(h, GWLP_USERDATA); if (m == WM_NCCREATE) { CREATESTRUCT* cs = (CREATESTRUCT*)l; SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams); return DefWindowProc(h, m, w, l); }if (!self)return DefWindowProc(h, m, w, l); return self->instanceProc(h, m, w, l); }

    LRESULT SelectionOverlay::instanceProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_LBUTTONDOWN: startSelect(GET_X_LPARAM(l), GET_Y_LPARAM(l)); break;
        case WM_MOUSEMOVE:   if (selecting_) updateSelect(GET_X_LPARAM(l), GET_Y_LPARAM(l)); break;
        case WM_LBUTTONUP:   finishSelect(); break;
        case WM_TIMER:
            if (w == FADE_TIMER_ID) {
                updateFade();
            }
            break;
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
                HBRUSH darkBrush = CreateSolidBrush(RGB(0, 0, 0));
                HBRUSH oldBrush = (HBRUSH)SelectObject(dc, darkBrush);
                FillRect(dc, &c, darkBrush);
                SelectObject(dc, oldBrush);
                DeleteObject(darkBrush);
                
                // 只在选择时绘制选择框
                if (selecting_) {
                    // 绘制选择框
                    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                    HPEN oldPen = (HPEN)SelectObject(dc, pen);
                    SetBkMode(dc, TRANSPARENT);
                    
                    MoveToEx(dc, start_.x, start_.y, nullptr);
                    LineTo(dc, cur_.x, start_.y);
                    LineTo(dc, cur_.x, cur_.y);
                    LineTo(dc, start_.x, cur_.y);
                    LineTo(dc, start_.x, start_.y);
                    
                    SelectObject(dc, oldPen);
                    DeleteObject(pen);
                    
                    // 显示尺寸信息
                    SetTextColor(dc, RGB(255, 255, 255));
                    SetBkColor(dc, RGB(0, 0, 0));
                    SetBkMode(dc, OPAQUE);
                    
                    int width = abs(cur_.x - start_.x);
                    int height = abs(cur_.y - start_.y);
                    wchar_t sizeText[32];
                    swprintf_s(sizeText, L"%d×%d", width, height);
                    
                    int textX = std::min(start_.x, cur_.x);
                    int textY = std::min(start_.y, cur_.y) - 25;
                    if (textY < 0) textY = std::min(start_.y, cur_.y) + 5;
                    
                    TextOut(dc, textX, textY, sizeText, (int)wcslen(sizeText));
                }
            }
            
            EndPaint(h, &ps); 
        }break;
        default:return DefWindowProc(h, m, w, l);
        }return 0;
    }

} // namespace screenshot_tool