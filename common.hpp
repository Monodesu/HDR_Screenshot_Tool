#pragma once

#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cmath>
#include <memory>
#include <optional>
#include <ranges>
#include <format>
#include <string_view>
#include <span>
#include <chrono>
#include <algorithm>
#include <cwchar>
#include <thread>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;
using namespace Gdiplus;
using namespace std::string_literals;
using namespace std::chrono;

// Resource IDs
#define IDI_TRAY_ICON 101
#define IDM_AUTOSTART 102
#define IDM_SAVE_TO_FILE 103
#define IDM_RELOAD    104
#define IDM_EXIT      105
#define WM_TRAY_ICON  (WM_USER + 1)
#define WM_HOTKEY_REGION 1001
#define WM_HOTKEY_FULLSCREEN 1002
