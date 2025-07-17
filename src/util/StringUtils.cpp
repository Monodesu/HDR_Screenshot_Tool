#include "StringUtils.hpp"
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool::StringUtils {

    std::wstring Utf8ToWide(std::string_view utf8)
    {
        if (utf8.empty()) return {};
        int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
            static_cast<int>(utf8.size()), nullptr, 0);
        if (wlen <= 0) return {};
        std::wstring wide(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
            static_cast<int>(utf8.size()), wide.data(), wlen);
        return wide;
    }

    std::string WideToUtf8(std::wstring_view wide)
    {
        if (wide.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
            static_cast<int>(wide.size()), nullptr, 0,
            nullptr, nullptr);
        if (len <= 0) return {};
        std::string utf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
            utf8.data(), len, nullptr, nullptr);
        return utf8;
    }

} // namespace screenshot_tool::StringUtils
