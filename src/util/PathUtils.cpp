#include "PathUtils.hpp"
#include "TimeUtils.hpp"
#include "../platform/WinHeaders.hpp"
#include <shlwapi.h>
#include <array>

#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;
namespace screenshot_tool {

    std::wstring PathUtils::GetExeDirW()
    {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        fs::path p(buf);
        return p.parent_path().wstring();
    }

    std::wstring PathUtils::ResolveSavePathW(std::wstring_view configuredPath)
    {
        fs::path p(configuredPath);
        if (p.is_relative()) p = fs::path(GetExeDirW()) / p;
        return p.wstring();
    }

    bool PathUtils::EnsureDirectory(const std::wstring& path)
    {
        std::error_code ec;
        fs::create_directories(path, ec);
        return fs::exists(path);
    }

    std::wstring PathUtils::MakeTimestampedPngNameW()
    {
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t name[64];
        swprintf_s(name, L"%04u%02u%02u_%02u%02u%02u.png", st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond);
        return name;
    }

} // namespace screenshot_tool
