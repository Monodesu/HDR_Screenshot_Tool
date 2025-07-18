#pragma once
#include "../platform/WinHeaders.hpp"
#include <functional>

namespace screenshot_tool {

    class SelectionOverlay {
    public:
        using Callback = std::function<void(const RECT&)>;

        bool Create(HINSTANCE hInst, HWND parent, Callback cb);
        ~SelectionOverlay(); // 析构函数清理资源
        void Hide();
        bool IsValid() const;
        void BeginSelect();
        void BeginSelectOnMonitor(const RECT& monitorRect); // 限制在特定显示器上选择
        
        // 设置背景图像（用于显示冻结的屏幕内容）
        void SetBackgroundImage(const uint8_t* imageData, int width, int height, int stride);
        
        // 开始等待背景图像准备就绪
        void StartWaitingForBackground(std::function<void()> backgroundCheckCallback);

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);
        void startSelect(int x, int y); void updateSelect(int x, int y); void finishSelect();
        void cancelSelection(); // 新增：取消选择的方法
        void ShowWithRect(const RECT& displayRect); // 统一的显示方法
        
        // 淡入淡出动画相关
        void startFadeIn();
        void startFadeToFullOpaque();  // 新增：启动淡入到完全不透明的动画
        void startFadeOut();
        void updateFade();
        void onFadeComplete();
        
        // 动画系统辅助方法
        void initializeAnimationSystem();  // 初始化动画系统
        UINT getDisplayRefreshRate();       // 获取显示器刷新率
        double getAnimationProgress();      // 获取当前动画进度(0.0-1.0)
        
        // 双缓冲绘制相关
        void createBackBuffer(int width, int height);
        void destroyBackBuffer();
        void renderToBackBuffer();
        
        // 字体相关
        void createFont();
        void destroyFont();
        void drawSizeText(HDC hdc, const RECT& selectionRect);

        HWND hwnd_ = nullptr; HWND parent_ = nullptr; Callback cb_;
        bool selecting_ = false; POINT start_{}; POINT cur_{}; 
        
        // 淡入淡出动画状态
        BYTE alpha_ = 0; 
        bool fadingIn_ = false; 
        bool fadingOut_ = false;
        bool fadingToFullOpaque_ = false;
        UINT_PTR timerId_ = 0;
        static constexpr UINT_PTR FADE_TIMER_ID = 1;
        static constexpr BYTE TARGET_ALPHA = 200;  // 目标透明度，保持视觉效果
        
        // 基于时间的动画系统
        LARGE_INTEGER animationStartTime_{};  // 动画开始时间（高精度）
        LARGE_INTEGER performanceFreq_{};     // 性能计数器频率
        static constexpr double FADE_DURATION = 0.35;  // 动画持续时间：350ms（平衡流畅和速度）
        UINT displayRefreshRate_ = 60;        // 显示器刷新率，默认60Hz
        UINT fadeInterval_ = 5;               // 超高频率定时器间隔（5ms约200Hz）
        
        // 背景检测定时器
        UINT_PTR backgroundCheckTimerId_ = 0;
        static constexpr UINT_PTR BACKGROUND_CHECK_TIMER_ID = 2;
        static constexpr UINT BACKGROUND_CHECK_INTERVAL = 100; // 100ms检测间隔
        std::function<void()> backgroundCheckCallback_;
        
        // 双缓冲绘制
        HDC memDC_ = nullptr;
        HBITMAP memBitmap_ = nullptr;
        HBITMAP oldBitmap_ = nullptr;
        int bufferWidth_ = 0;
        int bufferHeight_ = 0;
        
        // 字体资源
        HFONT sizeFont_ = nullptr;
        
        // 背景图像（冻结的屏幕内容）
        HBITMAP backgroundBitmap_ = nullptr;
        int backgroundWidth_ = 0;
        int backgroundHeight_ = 0;
        
        // 选择区域透明效果
        void createMaskForSelection();
        void createBackgroundBitmap(const uint8_t* imageData, int width, int height, int stride);
        void destroyBackgroundBitmap();
        
        // 其他状态变量
        bool notifyOnHide_ = false; 
        RECT selectedRect_{};
        RECT monitorConstraint_{}; // 显示器约束区域
        bool useMonitorConstraint_ = false; // 是否使用显示器约束
        
        // 虚拟桌面偏移信息（用于多屏幕支持）
        int virtualDesktopLeft_ = 0;
        int virtualDesktopTop_ = 0;
    };

} // namespace screenshot_tool