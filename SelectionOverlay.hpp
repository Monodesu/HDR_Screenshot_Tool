#pragma once
#include "common.hpp"

class SelectionOverlay {
private:
	HWND hwnd = nullptr;
	HWND messageWnd = nullptr;
	BYTE alpha = 0;
	bool fadingIn = false;
	bool fadingOut = false;
	bool notifyHide = false;
	bool isSelecting = false;
	POINT startPoint{}, endPoint{};
	HDC memDC = nullptr;
	HBITMAP memBitmap = nullptr;
	HBITMAP oldBitmap = nullptr;

public:
        RECT selectedRect{};
        HWND GetHwnd() const { return hwnd; }

        bool Create(HWND msgWnd) {
		messageWnd = msgWnd;
		WNDCLASS wc{
			.lpfnWndProc = WindowProc,
			.hInstance = GetModuleHandle(nullptr),
			.hCursor = LoadCursor(nullptr, IDC_CROSS),
			.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)),
			.lpszClassName = L"SelectionOverlay"
		};

		RegisterClass(&wc);

                int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

		hwnd = CreateWindowEx(
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
			L"SelectionOverlay", L"",
			WS_POPUP,
                        screenX, screenY, screenWidth, screenHeight,
                        nullptr, nullptr, GetModuleHandle(nullptr), this);

		if (!hwnd) return false;

		// 创建内存DC和位图以避免闪烁
		HDC hdc = GetDC(hwnd);
		memDC = CreateCompatibleDC(hdc);
		memBitmap = CreateCompatibleBitmap(hdc, screenWidth, screenHeight);
		oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));
		ReleaseDC(hwnd, hdc);

		ShowWindow(hwnd, SW_HIDE);
		SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		return true;
	}

	void Show() {
		auto style = GetWindowLong(hwnd, GWL_EXSTYLE);
		SetWindowLong(hwnd, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
		isSelecting = false;
		startPoint = endPoint = POINT{};
		alpha = 0;
		fadingOut = false;
		fadingIn = true;
		KillTimer(hwnd, 2);
		SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
		ShowWindow(hwnd, SW_SHOW);
		SetForegroundWindow(hwnd);
		SetTimer(hwnd, 1, 15, nullptr);
	}

	void Hide(bool notify = false) {
		if (!IsWindowVisible(hwnd)) return;
		notifyHide = notify;
		fadingIn = false;
		fadingOut = true;
		KillTimer(hwnd, 1);
		SetTimer(hwnd, 2, 15, nullptr);
	}

	void Destroy() {
		if (memDC) {
			SelectObject(memDC, oldBitmap);
			DeleteObject(memBitmap);
			DeleteDC(memDC);
		}
		if (hwnd) {
			DestroyWindow(hwnd);
			hwnd = nullptr;
		}
	}

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		auto* overlay = reinterpret_cast<SelectionOverlay*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		if (!overlay) return DefWindowProc(hwnd, msg, wParam, lParam);

		switch (msg) {
		case WM_LBUTTONDOWN:
			overlay->isSelecting = true;
			overlay->startPoint.x = GET_X_LPARAM(lParam);
			overlay->startPoint.y = GET_Y_LPARAM(lParam);
			overlay->endPoint = overlay->startPoint;
			SetCapture(hwnd);
			InvalidateRect(hwnd, nullptr, FALSE);
			break;

		case WM_RBUTTONDOWN:
			overlay->Hide();
			break;

		case WM_MOUSEMOVE:
			if (overlay->isSelecting) {
				overlay->endPoint.x = GET_X_LPARAM(lParam);
				overlay->endPoint.y = GET_Y_LPARAM(lParam);
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			break;

		case WM_LBUTTONUP:
			if (overlay->isSelecting) {
				overlay->isSelecting = false;
				ReleaseCapture();

				// 计算选择区域
				auto [minX, maxX] = std::minmax(overlay->startPoint.x, overlay->endPoint.x);
				auto [minY, maxY] = std::minmax(overlay->startPoint.y, overlay->endPoint.y);

				overlay->selectedRect = RECT{
					.left = minX,
					.top = minY,
					.right = maxX,
					.bottom = maxY
				};

				overlay->Hide(true);
			}
			break;

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE) {
				overlay->Hide();
			}
			break;

		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT clientRect;
			GetClientRect(hwnd, &clientRect);

			// 使用内存DC绘制以避免闪烁
			// 清除背景
			auto brush = CreateSolidBrush(RGB(0, 0, 0));
			FillRect(overlay->memDC, &clientRect, brush);
			DeleteObject(brush);

			// 绘制选择框
			if (overlay->isSelecting) {
				auto pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
				auto oldPen = SelectObject(overlay->memDC, pen);

				auto [minX, maxX] = std::minmax(overlay->startPoint.x, overlay->endPoint.x);
				auto [minY, maxY] = std::minmax(overlay->startPoint.y, overlay->endPoint.y);

				// 绘制选择框
				SetBkMode(overlay->memDC, TRANSPARENT);
				Rectangle(overlay->memDC, minX, minY, maxX, maxY);

				// 绘制尺寸信息
				auto oldTextColor = SetTextColor(overlay->memDC, RGB(255, 255, 255));
				auto font = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
				auto oldFont = SelectObject(overlay->memDC, font);

				auto width = abs(maxX - minX);
				auto height = abs(maxY - minY);
				auto text = std::format(L"{}×{}", width, height);

				TextOut(overlay->memDC, minX, minY - 25, text.c_str(), static_cast<int>(text.length()));

				SelectObject(overlay->memDC, oldFont);
				DeleteObject(font);
				SetTextColor(overlay->memDC, oldTextColor);
				SelectObject(overlay->memDC, oldPen);
				DeleteObject(pen);
			}

			// 将内存DC内容复制到窗口DC
			BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, overlay->memDC, 0, 0, SRCCOPY);

			EndPaint(hwnd, &ps);
			break;
		}

		case WM_TIMER:
			if (wParam == 1 && overlay->fadingIn) {
				overlay->alpha = static_cast<BYTE>(std::min<int>(overlay->alpha + 16, 128));
				SetLayeredWindowAttributes(hwnd, 0, overlay->alpha, LWA_ALPHA);
				if (overlay->alpha >= 128) {
					KillTimer(hwnd, 1);
					overlay->fadingIn = false;
				}
			}
			else if (wParam == 2 && overlay->fadingOut) {
				if (overlay->alpha <= 16) {
					overlay->alpha = 0;
					SetLayeredWindowAttributes(hwnd, 0, overlay->alpha, LWA_ALPHA);
					KillTimer(hwnd, 2);
					overlay->fadingOut = false;
					ShowWindow(hwnd, SW_HIDE);
					auto style2 = GetWindowLong(hwnd, GWL_EXSTYLE);
					SetWindowLong(hwnd, GWL_EXSTYLE, style2 | WS_EX_TRANSPARENT);
					overlay->isSelecting = false;
					if (overlay->notifyHide && overlay->messageWnd)
						PostMessage(overlay->messageWnd, WM_USER + 100, 0, 0);
					overlay->notifyHide = false;
				}
				else {
					overlay->alpha = static_cast<BYTE>(overlay->alpha - 16);
					SetLayeredWindowAttributes(hwnd, 0, overlay->alpha, LWA_ALPHA);
				}
			}
			break;

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		return 0;
	}
};

