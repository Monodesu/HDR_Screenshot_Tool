#include "../platform/WinHeaders.hpp"
#include "ScreenshotApp.hpp"

#include <crtdbg.h>

using namespace screenshot_tool;

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
#if defined(_DEBUG)
    // Debug CRT �ڴ��⣨��ѡ��
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    ScreenshotApp app;
    if (!app.Initialize(hInst)) {
        MessageBox(nullptr, L"��ʼ��ʧ��", L"HDR Screenshot Tool", MB_OK | MB_ICONERROR);
        return 1;
    }
    int code = app.Run();
    app.Shutdown();
    return code;
}

} // namespace screenshot_tool
