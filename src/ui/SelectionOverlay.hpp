#pragma once
#include "../platform/WinHeaders.hpp"
#include <functional>

// D3D11 �� D2D/DirectWrite ͷ�ļ�
#include <wrl/client.h>
#include <d3d11_1.h>
#include <d2d1_1.h>
#include <dwrite_1.h>

namespace screenshot_tool {

    class SelectionOverlay {
    public:
        using Callback = std::function<void(const RECT&)>;

        bool Create(HINSTANCE hInst, HWND parent, Callback cb);
        ~SelectionOverlay();
        void Hide();
        bool IsValid() const;
        void BeginSelect();
        void BeginSelectOnMonitor(const RECT& monitorRect);
        
        // ���ñ���ͼ��������ʾ�������Ļ���ݣ�
        void SetBackgroundImage(const uint8_t* imageData, int width, int height, int stride);
        
        // ��ʼ�ȴ�����ͼ��׼������
        void StartWaitingForBackground(std::function<void()> backgroundCheckCallback);

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);
        void startSelect(int x, int y); 
        void updateSelect(int x, int y); 
        void finishSelect();
        void cancelSelection();
        void ShowWithRect(const RECT& displayRect);
        
        // �������
        void startFadeIn();
        void startFadeOut();
        void updateFade();
        void initializeSimpleAnimation();
        
        // D3D��Ⱦ����ط���
        bool initializeD3DRenderer();
        void cleanupD3DRenderer();
        void resizeD3DRenderer(UINT width, UINT height);
        void renderWithD3D();
        void renderBackgroundWithD3D();
        void renderDarkenMaskWithD3D();
        void renderSelectionBoxWithD3D();
        void renderSizeTextWithD3D();
        void createD2DBitmapFromGDI(HBITMAP gdiBitmap, ID2D1Bitmap** d2dBitmap);
        
        // ����ͼ����
        void createBackgroundBitmap(const uint8_t* imageData, int width, int height, int stride);
        void destroyBackgroundBitmap();

        HWND hwnd_ = nullptr; 
        HWND parent_ = nullptr; 
        Callback cb_;
        bool selecting_ = false; 
        POINT start_{}; 
        POINT cur_{}; 
        
        // ����״̬
        BYTE alpha_ = 0; 
        bool fadingIn_ = false; 
        bool fadingOut_ = false;
        UINT_PTR timerId_ = 0;
        static constexpr UINT_PTR FADE_TIMER_ID = 1;
        static constexpr BYTE TARGET_ALPHA = 128;
        UINT fadeInterval_ = 15;
        
        // ������ⶨʱ��
        UINT_PTR backgroundCheckTimerId_ = 0;
        static constexpr UINT_PTR BACKGROUND_CHECK_TIMER_ID = 2;
        static constexpr UINT BACKGROUND_CHECK_INTERVAL = 100;
        std::function<void()> backgroundCheckCallback_;
        
        // D3D��Ⱦ����Դ - ������Ψһ����Ⱦ·��
        Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> d3dRenderTargetView_;
        Microsoft::WRL::ComPtr<IDXGISwapChain> dxgiSwapChain_;
        Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> d2dRenderTarget_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dWhiteBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dDarkBrush_;
        Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> dwriteTextFormat_;
        D3D_FEATURE_LEVEL d3dFeatureLevel_;
        
        // ����ͼ�񣨶������Ļ���ݣ�
        HBITMAP backgroundBitmap_ = nullptr;
        int backgroundWidth_ = 0;
        int backgroundHeight_ = 0;
        
        // ����״̬����
        bool notifyOnHide_ = false; 
        RECT selectedRect_{};
        RECT monitorConstraint_{}; 
        bool useMonitorConstraint_ = false;
        
        // ��������ƫ����Ϣ�����ڶ���Ļ֧�֣�
        int virtualDesktopLeft_ = 0;
        int virtualDesktopTop_ = 0;
        
        // �����Ż����
        DWORD lastUpdateTime_ = 0;
        static constexpr DWORD MIN_UPDATE_INTERVAL = 8; // ��С���¼�������룩
    };

} // namespace screenshot_tool