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

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT instanceProc(HWND, UINT, WPARAM, LPARAM);
        void startSelect(int x, int y); void updateSelect(int x, int y); void finishSelect();

        HWND hwnd_ = nullptr; HWND parent_ = nullptr; Callback cb_;
        bool selecting_ = false; POINT start_{}; POINT cur_{}; BYTE alpha_ = 0; bool fadingIn_ = false; bool fadingOut_ = false;
    };

} // namespace screenshot_tool