#include "TimeUtils.hpp"
#include "../platform/WinHeaders.hpp"

namespace screenshot_tool {
    std::wstring FormatTimestampForFilename() {
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"%04u-%02u-%02u_%02u-%02u-%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buf;
    }
} // namespace screenshot_tool