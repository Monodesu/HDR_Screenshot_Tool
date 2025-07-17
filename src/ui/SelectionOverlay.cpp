#include "SelectionOverlay.hpp"

namespace screenshot_tool {

    static const wchar_t* kOverlayClass = L"HDRShotOverlayWindow";

    bool SelectionOverlay::Create(HINSTANCE inst, HWND parent, Callback cb) {
        parent_ = parent; cb_ = std::move(cb);
        WNDCLASS wc{}; wc.lpfnWndProc = SelectionOverlay::WndProc; wc.hInstance = inst; wc.lpszClassName = kOverlayClass; wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        wc.hbrBackground = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
        RegisterClass(&wc);
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kOverlayClass, L"", WS_POPUP, 0, 0, 0, 0, parent, nullptr, inst, this);
        return hwnd_ != nullptr;
    }

    void SelectionOverlay::Show() {
        if (!hwnd_)return; RECT r; GetWindowRect(GetDesktopWindow(), &r);
        SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_SHOWWINDOW);
        alpha_ = 0; fadingIn_ = true; fadingOut_ = false; SetTimer(hwnd_, 1, 16, nullptr);
        SetLayeredWindowAttributes(hwnd_, 0, alpha_, LWA_ALPHA);
    }

    void SelectionOverlay::Hide() {
        if (!hwnd_)return; fadingOut_ = true; fadingIn_ = false; SetTimer(hwnd_, 2, 16, nullptr);
    }

    void SelectionOverlay::startSelect(int x, int y) { selecting_ = true; start_.x = x; start_.y = y; cur_ = start_; InvalidateRect(hwnd_, nullptr, FALSE); }
    void SelectionOverlay::updateSelect(int x, int y) { cur_.x = x; cur_.y = y; InvalidateRect(hwnd_, nullptr, FALSE); }
    void SelectionOverlay::finishSelect() { if (!selecting_)return; selecting_ = false; RECT rc{ min(start_.x,cur_.x),min(start_.y,cur_.y),max(start_.x,cur_.x),max(start_.y,cur_.y) }; Hide(); if (cb_)cb_(rc); }

    LRESULT CALLBACK SelectionOverlay::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { SelectionOverlay* self = (SelectionOverlay*)GetWindowLongPtr(h, GWLP_USERDATA); if (m == WM_NCCREATE) { CREATESTRUCT* cs = (CREATESTRUCT*)l; SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams); return DefWindowProc(h, m, w, l); }if (!self)return DefWindowProc(h, m, w, l); return self->instanceProc(h, m, w, l); }

    LRESULT SelectionOverlay::instanceProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_LBUTTONDOWN: startSelect(GET_X_LPARAM(l), GET_Y_LPARAM(l)); break;
        case WM_MOUSEMOVE:   if (selecting_) updateSelect(GET_X_LPARAM(l), GET_Y_LPARAM(l)); break;
        case WM_LBUTTONUP:   finishSelect(); break;
        case WM_TIMER:
            if (w == 1) { if (fadingIn_) { if (alpha_ < 200)alpha_ += 10; else KillTimer(h, 1); } SetLayeredWindowAttributes(h, 0, alpha_, LWA_ALPHA); }
            else if (w == 2) { if (fadingOut_) { if (alpha_ > 0)alpha_ -= 10; else { KillTimer(h, 2); ShowWindow(h, SW_HIDE); } SetLayeredWindowAttributes(h, 0, alpha_, LWA_ALPHA); } }
            break;
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT c; GetClientRect(h, &c); HBRUSH br = CreateSolidBrush(RGB(0, 0, 0)); SetBkMode(dc, TRANSPARENT); FrameRect(dc, &c, br); DeleteObject(br); Rectangle(dc, start_.x, start_.y, cur_.x, cur_.y); EndPaint(h, &ps); }break;
        default:return DefWindowProc(h, m, w, l);
        }return 0;
    }

} // namespace screenshot_tool