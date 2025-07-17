#pragma once

#include <string>
#include <string_view>
#include <filesystem>

#include "StringUtils.hpp"  // Utf8ToWide / WideToUtf8

namespace screenshot_tool {

    class PathUtils {
    public:
        // 返回当前可执行文件所在目录（宽字符串）
        static std::wstring GetExeDirW();

        // 将配置中可能是相对路径的保存目录解析为绝对路径（宽字符串）
        static std::wstring ResolveSavePathW(std::wstring_view configuredPath);

        // 确保目录存在，若不存在递归创建；返回是否存在/创建成功
        static bool EnsureDirectory(const std::wstring& path);

        // 生成 yyyyMMdd_HHmmss.png 的文件名（宽字符串）
        static std::wstring MakeTimestampedPngNameW();

        // -------- UTF‑8 便利封装 --------
        static std::string ResolveSavePath(std::string_view configuredUtf8Path) {
            auto resolvedW = ResolveSavePathW(StringUtils::Utf8ToWide(configuredUtf8Path));
            return StringUtils::WideToUtf8(resolvedW);
        }
    };

} // namespace screenshot_tool