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
        TemporaryFailure,
        NeedsReinitialize,
        NotSupported
    };

} // namespace screenshot_tool