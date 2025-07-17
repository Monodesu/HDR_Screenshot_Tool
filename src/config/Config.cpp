#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace screenshot_tool {

    static std::string trim_copy(std::string s) {
        auto isspace_ = [](unsigned char c) {return std::isspace(c) != 0; };
        s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), isspace_));
        s.erase(std::find_if_not(s.rbegin(), s.rend(), isspace_).base(), s.end());
        return s;
    }

    bool LoadConfig(Config& cfg, const std::wstring& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            auto key = trim_copy(line.substr(0, pos));
            auto val = trim_copy(line.substr(pos + 1));
            if (key == "RegionHotkey") cfg.regionHotkey = val;
            else if (key == "FullscreenHotkey") cfg.fullscreenHotkey = val;
            else if (key == "SavePath") cfg.savePath = val;
            else if (key == "SaveToFile") cfg.saveToFile = (val == "true" || val == "1");
            else if (key == "AutoCreateSaveDir") cfg.autoCreateSaveDir = (val == "true" || val == "1");
            else if (key == "AutoStart") cfg.autoStart = (val == "true" || val == "1");
            else if (key == "DebugMode") cfg.debugMode = (val == "true" || val == "1");
            else if (key == "UseACESFilmToneMapping") cfg.useACESFilmToneMapping = (val == "true" || val == "1");
            else if (key == "SDRBrightness") cfg.sdrBrightness = std::clamp(std::stof(val), 80.0f, 1000.0f);
            else if (key == "FullscreenCurrentMonitor") cfg.fullscreenCurrentMonitor = (val == "true" || val == "1");
            else if (key == "RegionFullscreenMonitor") cfg.regionFullscreenMonitor = (val == "true" || val == "1");
            else if (key == "CaptureRetryCount") cfg.captureRetryCount = std::clamp(std::stoi(val), 1, 10);
        }
        return true;
    }

    bool SaveConfig(const Config& cfg, const std::wstring& path) {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << "; HDR Screenshot Tool Configuration\n";
        f << "RegionHotkey=" << cfg.regionHotkey << '\n';
        f << "FullscreenHotkey=" << cfg.fullscreenHotkey << '\n';
        f << "SavePath=" << cfg.savePath << '\n';
        f << "SaveToFile=" << (cfg.saveToFile ? "true" : "false") << '\n';
        f << "AutoCreateSaveDir=" << (cfg.autoCreateSaveDir ? "true" : "false") << '\n';
        f << "AutoStart=" << (cfg.autoStart ? "true" : "false") << '\n';
        f << "DebugMode=" << (cfg.debugMode ? "true" : "false") << '\n';
        f << "UseACESFilmToneMapping=" << (cfg.useACESFilmToneMapping ? "true" : "false") << '\n';
        f << "SDRBrightness=" << cfg.sdrBrightness << '\n';
        f << "FullscreenCurrentMonitor=" << (cfg.fullscreenCurrentMonitor ? "true" : "false") << '\n';
        f << "RegionFullscreenMonitor=" << (cfg.regionFullscreenMonitor ? "true" : "false") << '\n';
        f << "CaptureRetryCount=" << cfg.captureRetryCount << '\n';
        return true;
    }

    bool EnsureConfigFile(const Config& cfg, const std::wstring& path) {
        // 检查文件是否存在
        std::ifstream testFile(path);
        bool fileExists = testFile.good();
        testFile.close();
        
        if (!fileExists) {
            // 文件不存在，创建默认配置文件
            return SaveConfig(cfg, path);
        }
        
        // 文件存在，检查是否包含所有必要的配置项
        Config tempCfg;
        if (LoadConfig(tempCfg, path)) {
            // 重新保存以确保包含所有最新的配置项和注释
            return SaveConfig(tempCfg, path);
        }
        
        return false;
    }

} // namespace screenshot_tool