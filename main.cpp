#include "ScreenshotApp.hpp"
#include "common.hpp"
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	// 防止多实例运行
	auto hMutex = CreateMutex(nullptr, TRUE, L"HDRScreenshotTool_Mutex");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(hMutex);
		return 0;
	}

	auto mutexGuard = [hMutex](void*) { CloseHandle(hMutex); };
	std::unique_ptr<void, decltype(mutexGuard)> guard(reinterpret_cast<void*>(1), mutexGuard);

	// 初始化GDI+
	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

	auto gdiplusGuard = [gdiplusToken](void*) { GdiplusShutdown(gdiplusToken); };
	std::unique_ptr<void, decltype(gdiplusGuard)> gdiplusCleanup(reinterpret_cast<void*>(1), gdiplusGuard);

	// 初始化COM
	CoInitialize(nullptr);
	auto comGuard = [](void*) { CoUninitialize(); };
	std::unique_ptr<void, decltype(comGuard)> comCleanup(reinterpret_cast<void*>(1), comGuard);

	// 创建并运行应用程序
	auto app = std::make_unique<ScreenshotApp>();
	if (app->Initialize()) {
		app->Run();
	}

	app->Cleanup();

	return 0;
}