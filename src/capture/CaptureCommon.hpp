#pragma once
#include <cstdint>

namespace screenshot_tool {

    struct HDRMetadata {
        float maxLuminance = 1000.0f;
        float minLuminance = 0.1f;
        float maxContentLightLevel = 1000.0f;
    };

    enum class CaptureResult {
        Success,
        TemporaryFailure,      // 可重试，不需要重新初始化
        NeedsReinitialization, // 需要重新初始化 (设备丢失、配置变化等)
        NotSupported          // 完全不支持，需要fallback
    };

} // namespace screenshot_tool