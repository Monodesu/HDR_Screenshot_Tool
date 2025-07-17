#pragma once
#include <string>

namespace screenshot_tool {

    struct Config {
        // �ȼ����ַ�����ʽ���Ժ��� HotkeyParse ������
        std::string regionHotkey = "ctrl+alt+a";          // �����ͼ
        std::string fullscreenHotkey = "ctrl+shift+alt+a";// ȫ����ͼ

        // ·�� & ����
        std::string savePath = "Screenshots";             // ��Ի����·��
        bool        saveToFile = true;                     // ��ͼ�Ƿ�д�ļ�
        bool        autoCreateSaveDir = true;              // �Զ�����Ŀ¼����ĿҪ���ǣ�

        // �Զ�����
        bool        autoStart = false;                     // ������ݷ�ʽ

        // ����
        bool        debugMode = false;                     // д������־

        // HDR����
        bool        useACESFilmToneMapping = false;
        float       sdrBrightness = 250.0f;                // SDR ӳ��Ŀ���ֵ���� (nit)

        // ����ʾ����Ϊ
        bool        fullscreenCurrentMonitor = false;      // true: ȫ����ͼ��ǰ��ʾ����false: ������ʾ��
        bool        regionFullscreenMonitor = false;       // true: ����ѡ�����Ƶ�ǰ��ʾ����false: ����ʾ��ѡ��

        // ����
        int         captureRetryCount = 3;                 // DXGI ���Դ���
    };

    // �� ini ·���������ã����ļ�������������Ĭ�ϡ�
    bool LoadConfig(Config& cfg, const std::wstring& path = L"config.ini");

    // �������õ� ini����ʧ�ܷ��� false��
    bool SaveConfig(const Config& cfg, const std::wstring& path = L"config.ini");

    // ȷ�������ļ����ڲ������°汾���������������
    bool EnsureConfigFile(const Config& cfg, const std::wstring& path = L"config.ini");

} // namespace screenshot_tool