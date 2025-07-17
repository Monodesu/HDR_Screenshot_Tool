#include "Logger.hpp"
#include "../platform/WinHeaders.hpp"
#include <fstream>

namespace screenshot_tool {

    Logger& Logger::Get() {
        static Logger g; return g;
    }

    void Logger::EnableFileLogging(const std::wstring& path) {
        std::scoped_lock lk(Get().mtx_);
        Get().filePath_ = path;
    }

    void Logger::writeLine(const std::wstring& line) {
        OutputDebugStringW((line + L"\n").c_str());
        if (!filePath_.empty()) {
            std::wofstream f(filePath_, std::ios::app);
            if (f.is_open()) f << line << L"\n";
        }
    }

} // namespace screenshot_tool