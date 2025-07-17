#pragma once
#include <string>
#include <string_view>
#include <filesystem>

namespace screenshot_tool {

	// 将用户配置的保存路径转绝对路径；若是相对路径，以当前 exe 所在目录为基准。
	std::wstring ResolveSavePath(std::wstring_view configuredPath);

	// 确保目录存在（创建多级目录）。
	bool EnsureDirectory(const std::wstring& path);

	// 根据时间戳生成文件名（YYYYMMDD_HHMMSS.png）。
	std::wstring MakeTimestampedPngName();

} // namespace screenshot_tool