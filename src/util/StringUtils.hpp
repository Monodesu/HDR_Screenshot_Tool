#pragma once

#include <string>
#include <string_view>

namespace screenshot_tool {

	class StringUtils {
	public:
		// UTF-8 → UTF-16
		static std::wstring Utf8ToWide(std::string_view utf8);

		// UTF-16 → UTF-8
		static std::string WideToUtf8(std::wstring_view wide);
	};

} // namespace screenshot_tool