#pragma once
#include <string>
#include <string_view>
#include <format>
#include <mutex>

namespace screenshot_tool {

    // 调试日志类，输出到调试器 + 可选写文件（根据 config.debugMode开启）
    class Logger {
    public:
        static Logger& Get();
        static void EnableFileLogging(const std::wstring& path); // 启用文件输出

        template<class...Args>
        static void Info(std::wstring_view fmt, Args&&...args) { Get().logImpl(L"INFO", fmt, std::forward<Args>(args)...); }
        template<class...Args>
        static void Warn(std::wstring_view fmt, Args&&...args) { Get().logImpl(L"WARN", fmt, std::forward<Args>(args)...); }
        template<class...Args>
        static void Error(std::wstring_view fmt, Args&&...args) { Get().logImpl(L"ERR", fmt, std::forward<Args>(args)...); }
        template<class...Args>
        static void Debug(std::wstring_view fmt, Args&&...args) { Get().logImpl(L"DBG", fmt, std::forward<Args>(args)...); }

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