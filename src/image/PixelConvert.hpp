#pragma once
#include "ImageBuffer.hpp"

namespace screenshot_tool {

	// ������֧�ָ�ʽת��Ϊ 8bit RGB����� data vector��
	bool ConvertToRGB8(const ImageBuffer& in, ImageBuffer& outRGB8);

} // namespace screenshot_tool