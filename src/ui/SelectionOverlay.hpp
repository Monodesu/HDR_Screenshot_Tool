#pragma once
#include "../platform/WinHeaders.hpp"
#include <functional>

namespace screenshot_tool {

    class SelectionOverlay {
    public:
        using Callback = std::function<void(const RECT&)>;

        bool Create(HINSTANCE hInst, HWND parent, Callback cb);
        void Show();
        void Hide();
        bool IsValid() const;
        void BeginSelect();
        void BeginSelectOnMonitor(const RECT& monitorRect); // 限制在特定显示器上选择

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);
        void startSelect(int x, int y); void updateSelect(int x, int y); void finishSelect();
        void ShowOnMonitor(const RECT& monitorRect); // 在特定显示器上显示

        HWND hwnd_ = nullptr; HWND parent_ = nullptr; Callback cb_;
        bool selecting_ = false; POINT start_{}; POINT cur_{}; BYTE alpha_ = 0; bool fadingIn_ = false; bool fadingOut_ = false;
        bool notifyOnHide_ = false; RECT selectedRect_{};
        RECT monitorConstraint_{}; // 显示器约束区域
        bool useMonitorConstraint_ = false; // 是否使用显示器约束
    };

} // namespace screenshot_tool