#pragma once
#include <string>

namespace screenshot_tool {

    struct Config {
        // 热键（字符串形式，稍后由 HotkeyParse 解析）
        std::string regionHotkey = "ctrl+alt+a";          // 区域截图
        std::string fullscreenHotkey = "ctrl+shift+alt+a";// 全屏截图

        // 路径 & 保存
        std::string savePath = "Screenshots";             // 相对或绝对路径
        bool        saveToFile = true;                     // 截图是否写文件
        bool        autoCreateSaveDir = true;              // 自动创建目录（项目要求：是）

        // 自动启动
        bool        autoStart = false;                     // 启动快捷方式

        // 通知 & 调试
        bool        showNotification = true;
        bool        debugMode = false;                     // 写调试日志

        // HDR处理
        bool        useACESFilmToneMapping = false;
        float       sdrBrightness = 250.0f;                // SDR 映射目标峰值亮度 (nit)

        // 多显示器行为
        bool        fullscreenCurrentMonitor = false;      // 全屏默认当前监视器
        bool        regionFullscreenMonitor = true;        // 区域内自动按监视器裁剪

        // 捕获
        int         captureRetryCount = 3;                 // DXGI 重试次数
    };

    // 从 ini 路径加载配置；若文件不存在则沿用默认。
    bool LoadConfig(Config& cfg, const std::wstring& path = L"config.ini");

    // 保存配置到 ini；若失败返回 false。
    bool SaveConfig(const Config& cfg, const std::wstring& path = L"config.ini");

} // namespace screenshot_tool