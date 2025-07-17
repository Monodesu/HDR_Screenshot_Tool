#include "PathUtils.hpp"
#include "TimeUtils.hpp"
#include "../platform/WinHeaders.hpp"
#include <shlwapi.h>
#include <array>

#pragma comment(lib, "shlwapi.lib")

namespace screenshot_tool {

    static std::wstring GetExeDir() {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::filesystem::path p(buf);
        return p.parent_path().wstring();
    }

    std::wstring ResolveSavePath(std::wstring_view configuredPath) {
        std::filesystem::path p(configuredPath);
        if (p.is_relative()) p = GetExeDir() / p;
        return p.wstring();
    }

    bool EnsureDirectory(const std::wstring& path) {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return std::filesystem::exists(path);
    }

    std::wstring MakeTimestampedPngName() {
        SYSTEMTIME st = {}; GetLocalTime(&st);
        wchar_t name[64];
        swprintf_s(name, L"%04u%02u%02u_%02u%02u%02u.png", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return name;
    }

} // namespace screenshot_tool