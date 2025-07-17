#pragma once
#include <string>
#include <string_view>
#include <filesystem>

namespace screenshot_tool {

	// ���û����õı���·��ת����·�����������·�����Ե�ǰ exe ����Ŀ¼Ϊ��׼��
	std::wstring ResolveSavePath(std::wstring_view configuredPath);

	// ȷ��Ŀ¼���ڣ������༶Ŀ¼����
	bool EnsureDirectory(const std::wstring& path);

	// ����ʱ��������ļ�����YYYYMMDD_HHMMSS.png����
	std::wstring MakeTimestampedPngName();

} // namespace screenshot_tool