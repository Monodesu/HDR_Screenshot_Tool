#pragma once
#include <string>
#include <string_view>
#include <format>
#include <mutex>

namespace screenshot_tool {

    // 简易日志：调试输出 + 可选写文件（依 config.debugMode）。
    class Logger {
    public:
        static Logger& Get();
        static void EnableFileLogging(const std::wstring& path); // 传空则关闭

        template<class...Args>
        static void Info(std::wstring_view fmt, Args&&...args) { logImpl(L"INFO", fmt, std::forward<Args>(args)...); }
        template<class...Args>
        static void Warn(std::wstring_view fmt, Args&&...args) { logImpl(L"WARN", fmt, std::forward<Args>(args)...); }
        template<class...Args>
        static void Error(std::wstring_view fmt, Args&&...args) { logImpl(L"ERR", fmt, std::forward<Args>(args)...); }
        template<class...Args>
        static void Debug(std::wstring_view fmt, Args&&...args) { logImpl(L"DBG", fmt, std::forward<Args>(args)...); }

    private:
        Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        void writeLine(const std::wstring& line);

        template<class...Args>
        void logImpl(std::wstring_view lvl, std::wstring_view fmt, Args&&...args) {
            std::wstring msg = std::vformat(fmt, std::make_wformat_args(args...));
            writeLine(std::wstring(lvl) + L": " + msg);
        }

        std::mutex      mtx_;
        std::wstring    filePath_;
    };

} // namespace screenshot_tool