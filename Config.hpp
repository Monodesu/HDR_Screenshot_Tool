#pragma once
#include "common.hpp"

struct Config {
    std::string regionHotkey = "ctrl+alt+a";
    std::string fullscreenHotkey = "ctrl+shift+alt+a";
    std::string savePath = "Screenshots";
    bool autoStart = false;
    bool saveToFile = true;
    bool showNotification = true;       // 是否弹窗提示
    bool debugMode = false;             // 调试模式
    bool useACESFilmToneMapping = false; // 使用ACES色调映射
    float sdrBrightness = 250.0f;       // SDR目标亮度
    bool fullscreenCurrentMonitor = false; // 仅截取指针所在显示器
    bool regionFullscreenMonitor = true;   // 全屏程序下区域截图是否截取当前显示器
    int captureRetryCount = 3;             // 截图失败重试次数
};
