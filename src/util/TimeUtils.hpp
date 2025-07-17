#pragma once
#include <string>

namespace screenshot_tool {

	class TimeUtils {
	public:
		// 生成时间戳字符串，格式: yyyy-MM-dd_HH-mm-ss
		static std::wstring FormatTimestampForFilename();
	};

} // namespace screenshot_tool