#pragma once
#include "../platform/WinHeaders.hpp"
#include <functional>

namespace screenshot_tool {

    class SelectionOverlay {
    public:
        using Callback = std::function<void(const RECT&)>;

        bool Create(HINSTANCE hInst, HWND parent, Callback cb);
        ~SelectionOverlay(); // ��������������Դ
        void Hide();
        bool IsValid() const;
        void BeginSelect();
        void BeginSelectOnMonitor(const RECT& monitorRect); // �������ض���ʾ����ѡ��
        
        // ���ñ���ͼ��������ʾ�������Ļ���ݣ�
        void SetBackgroundImage(const uint8_t* imageData, int width, int height, int stride);
        
        // ��ʼ�ȴ�����ͼ��׼������
        void StartWaitingForBackground(std::function<void()> backgroundCheckCallback);

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);
        void startSelect(int x, int y); void updateSelect(int x, int y); void finishSelect();
        void cancelSelection(); // ������ȡ��ѡ��ķ���
        void ShowWithRect(const RECT& displayRect); // ͳһ����ʾ����
        
        // ���뵭���������
        void startFadeIn();
        void startFadeToFullOpaque();  // �������������뵽��ȫ��͸���Ķ���
        void startFadeOut();
        void updateFade();
        void onFadeComplete();
        
        // ˫����������
        void createBackBuffer(int width, int height);
        void destroyBackBuffer();
        void renderToBackBuffer();
        
        // �������
        void createFont();
        void destroyFont();
        void drawSizeText(HDC hdc, const RECT& selectionRect);

        HWND hwnd_ = nullptr; HWND parent_ = nullptr; Callback cb_;
        bool selecting_ = false; POINT start_{}; POINT cur_{}; 
        
        // ���뵭������״̬
        BYTE alpha_ = 0; 
        bool fadingIn_ = false; 
        bool fadingOut_ = false;
        bool fadingToFullOpaque_ = false;  // ���������뵽��ȫ��͸����״̬
        UINT_PTR timerId_ = 0;
        static constexpr UINT_PTR FADE_TIMER_ID = 1;
        static constexpr BYTE TARGET_ALPHA = 200;  // Ŀ��͸����
        static constexpr BYTE FADE_STEP = 40;      // ��׼���ƣ�5֡���(0��40��80��120��160��200)
        static constexpr UINT FADE_INTERVAL = 16; // 60FPS��ȷ��������
        
        // ������ⶨʱ��
        UINT_PTR backgroundCheckTimerId_ = 0;
        static constexpr UINT_PTR BACKGROUND_CHECK_TIMER_ID = 2;
        static constexpr UINT BACKGROUND_CHECK_INTERVAL = 100; // 100ms�����
        std::function<void()> backgroundCheckCallback_;
        
        // ˫�������
        HDC memDC_ = nullptr;
        HBITMAP memBitmap_ = nullptr;
        HBITMAP oldBitmap_ = nullptr;
        int bufferWidth_ = 0;
        int bufferHeight_ = 0;
        
        // ������Դ
        HFONT sizeFont_ = nullptr;
        
        // ����ͼ�񣨶������Ļ���ݣ�
        HBITMAP backgroundBitmap_ = nullptr;
        int backgroundWidth_ = 0;
        int backgroundHeight_ = 0;
        
        // ѡ������͸��Ч��
        void createMaskForSelection();
        void createBackgroundBitmap(const uint8_t* imageData, int width, int height, int stride);
        void destroyBackgroundBitmap();
        
        // ����״̬����
        bool notifyOnHide_ = false; 
        RECT selectedRect_{};
        RECT monitorConstraint_{}; // ��ʾ��Լ������
        bool useMonitorConstraint_ = false; // �Ƿ�ʹ����ʾ��Լ��
        
        // ��������ƫ����Ϣ�����ڶ���Ļ֧�֣�
        int virtualDesktopLeft_ = 0;
        int virtualDesktopTop_ = 0;
    };

} // namespace screenshot_tool