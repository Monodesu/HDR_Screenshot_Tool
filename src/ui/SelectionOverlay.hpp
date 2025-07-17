#pragma once
#include "../platform/WinHeaders.hpp"
#include <functional>

namespace screenshot_tool {

    class SelectionOverlay {
    public:
        using Callback = std::function<void(const RECT&)>;

        bool Create(HINSTANCE hInst, HWND parent, Callback cb);
        ~SelectionOverlay(); // ��������������Դ
        void Show();
        void Hide();
        bool IsValid() const;
        void BeginSelect();
        void BeginSelectOnMonitor(const RECT& monitorRect); // �������ض���ʾ����ѡ��

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);
        void startSelect(int x, int y); void updateSelect(int x, int y); void finishSelect();
        void ShowOnMonitor(const RECT& monitorRect); // ���ض���ʾ������ʾ
        
        // ���뵭���������
        void startFadeIn();
        void startFadeOut();
        void updateFade();
        void onFadeComplete();

        HWND hwnd_ = nullptr; HWND parent_ = nullptr; Callback cb_;
        bool selecting_ = false; POINT start_{}; POINT cur_{}; 
        
        // ���뵭������״̬
        BYTE alpha_ = 0; 
        bool fadingIn_ = false; 
        bool fadingOut_ = false;
        UINT_PTR timerId_ = 0;
        static constexpr UINT_PTR FADE_TIMER_ID = 1;
        static constexpr BYTE TARGET_ALPHA = 200;  // Ŀ��͸����
        static constexpr BYTE FADE_STEP = 20;      // ÿ�ε��뵭���Ĳ���
        static constexpr UINT FADE_INTERVAL = 16; // Լ60FPS�ĸ��¼��
        
        bool notifyOnHide_ = false; RECT selectedRect_{};
        RECT monitorConstraint_{}; // ��ʾ��Լ������
        bool useMonitorConstraint_ = false; // �Ƿ�ʹ����ʾ��Լ��
    };

} // namespace screenshot_tool