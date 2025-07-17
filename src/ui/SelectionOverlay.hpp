#pragma once
#include "../platform/WinHeaders.hpp"
#include <functional>

namespace screenshot_tool {

    class SelectionOverlay {
    public:
        using Callback = std::function<void(const RECT&)>;

        bool Create(HINSTANCE hInst, HWND parent, Callback cb);
        ~SelectionOverlay(); // 析构函数清理资源
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
        
        // 淡入淡出动画相关
        void startFadeIn();
        void startFadeOut();
        void updateFade();
        void onFadeComplete();

        HWND hwnd_ = nullptr; HWND parent_ = nullptr; Callback cb_;
        bool selecting_ = false; POINT start_{}; POINT cur_{}; 
        
        // 淡入淡出动画状态
        BYTE alpha_ = 0; 
        bool fadingIn_ = false; 
        bool fadingOut_ = false;
        UINT_PTR timerId_ = 0;
        static constexpr UINT_PTR FADE_TIMER_ID = 1;
        static constexpr BYTE TARGET_ALPHA = 200;  // 目标透明度
        static constexpr BYTE FADE_STEP = 20;      // 每次淡入淡出的步长
        static constexpr UINT FADE_INTERVAL = 16; // 约60FPS的更新间隔
        
        bool notifyOnHide_ = false; RECT selectedRect_{};
        RECT monitorConstraint_{}; // 显示器约束区域
        bool useMonitorConstraint_ = false; // 是否使用显示器约束
    };

} // namespace screenshot_tool