#include "PixelConvert.hpp"
#include "ToneMapping.hpp"
#include "ColorSpace.hpp"
#include "../util/Logger.hpp"
#include <algorithm>
#include <ranges>

namespace screenshot_tool {

    bool PixelConvert::ConvertToRGB8(const ImageBuffer& in, ImageBuffer& out) {
        if (in.format == PixelFormat::RGB8) { 
            out = in; 
            return true; 
        }
        
        out.format = PixelFormat::RGB8; 
        out.width = in.width; 
        out.height = in.height; 
        out.stride = in.width * 3; 
        out.data.resize(out.stride * out.height);
        
        switch (in.format) {
        case PixelFormat::BGRA8:
            convertBGRA8ToRGB8(in, out);
            break;
        case PixelFormat::RGBA_F16:
            convertRGBA16FToRGB8(in, out);
            break;
        case PixelFormat::RGBA10A2:
            convertRGBA10A2ToRGB8(in, out);
            break;
        default:
            Logger::Error(L"Unsupported input format for conversion");
            return false;
        }
        
        return true;
    }

    bool PixelConvert::ToSRGB8(DXGI_FORMAT fmt, ImageBuffer& buffer, bool isHDR, const Config* config) {
        switch (fmt) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            if (isHDR) {
                processHDR16Float(buffer, config);
            } else {
                processSDR16Float(buffer);
            }
            break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            if (isHDR) {
                processHDR10(buffer, config);
            } else {
                processSDR10(buffer);
            }
            break;
        default:
            processSDR(buffer);
            break;
        }
        return true;
    }
    
    void PixelConvert::convertBGRA8ToRGB8(const ImageBuffer& in, ImageBuffer& out) {
        for (int y = 0; y < in.height; ++y) {
            const auto* srcRow = in.data.data() + y * in.stride;
            auto* dstRow = out.data.data() + y * out.stride;
            
            for (int x = 0; x < in.width; ++x) {
                dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // R
                dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
                dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // B
            }
        }
    }
    
    void PixelConvert::convertRGBA16FToRGB8(const ImageBuffer& in, ImageBuffer& out) {
        for (int y = 0; y < in.height; ++y) {
            const auto* srcRow = reinterpret_cast<const uint16_t*>(in.data.data() + y * in.stride);
            auto* dstRow = out.data.data() + y * out.stride;
            
            for (int x = 0; x < in.width; ++x) {
                float r = HalfToFloat(srcRow[x * 4 + 0]);
                float g = HalfToFloat(srcRow[x * 4 + 1]);
                float b = HalfToFloat(srcRow[x * 4 + 2]);
                
                // Clamp to 0-1 range for SDR
                r = std::clamp(r, 0.0f, 1.0f);
                g = std::clamp(g, 0.0f, 1.0f);
                b = std::clamp(b, 0.0f, 1.0f);
                
                // Apply gamma correction
                r = LinearToSRGB(r);
                g = LinearToSRGB(g);
                b = LinearToSRGB(b);
                
                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
    }
    
    void PixelConvert::convertRGBA10A2ToRGB8(const ImageBuffer& in, ImageBuffer& out) {
        for (int y = 0; y < in.height; ++y) {
            const auto* srcRow = reinterpret_cast<const uint32_t*>(in.data.data() + y * in.stride);
            auto* dstRow = out.data.data() + y * out.stride;
            
            for (int x = 0; x < in.width; ++x) {
                uint32_t pixel = srcRow[x];
                uint32_t r10 = (pixel >> 20) & 0x3FF;
                uint32_t g10 = (pixel >> 10) & 0x3FF;
                uint32_t b10 = pixel & 0x3FF;
                
                // SDR模式下简单缩放
                float r = static_cast<float>(r10) / 1023.0f;
                float g = static_cast<float>(g10) / 1023.0f;
                float b = static_cast<float>(b10) / 1023.0f;
                
                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
    }
    
    void PixelConvert::processHDR16Float(ImageBuffer& buffer, const Config* config) {
        // Convert in-place from RGBA16F to RGB8
        const int pixelCount = buffer.width * buffer.height;
        std::vector<uint8_t> rgbBuffer(pixelCount * 3);
        
        float targetNits = config ? config->sdrBrightness : 250.0f;
        float maxNits = 1000.0f; // Default HDR max
        float exposure = targetNits / maxNits;
        
        for (int y = 0; y < buffer.height; ++y) {
            const auto* srcRow = reinterpret_cast<const uint16_t*>(buffer.data.data() + y * buffer.stride);
            auto* dstRow = rgbBuffer.data() + y * buffer.width * 3;
            
            for (int x = 0; x < buffer.width; ++x) {
                float r = HalfToFloat(srcRow[x * 4 + 0]) * exposure;
                float g = HalfToFloat(srcRow[x * 4 + 1]) * exposure;
                float b = HalfToFloat(srcRow[x * 4 + 2]) * exposure;
                
                // Tone mapping
                if (config && config->useACESFilmToneMapping) {
                    float rgb[3] = {r, g, b};
                    ToneMap_ACES(rgb, rgb, 1);
                    r = rgb[0]; g = rgb[1]; b = rgb[2];
                } else {
                    r = r / (1.0f + r);
                    g = g / (1.0f + g);
                    b = b / (1.0f + b);
                }
                
                // sRGB gamma correction
                r = LinearToSRGB(r);
                g = LinearToSRGB(g);
                b = LinearToSRGB(b);
                
                dstRow[x * 3 + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
                dstRow[x * 3 + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
                dstRow[x * 3 + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
            }
        }
        
        // Update buffer
        buffer.format = PixelFormat::RGB8;
        buffer.stride = buffer.width * 3;
        buffer.data = std::move(rgbBuffer);
    }
    
    void PixelConvert::processSDR16Float(ImageBuffer& buffer) {
        const int pixelCount = buffer.width * buffer.height;
        std::vector<uint8_t> rgbBuffer(pixelCount * 3);
        
        for (int y = 0; y < buffer.height; ++y) {
            const auto* srcRow = reinterpret_cast<const uint16_t*>(buffer.data.data() + y * buffer.stride);
            auto* dstRow = rgbBuffer.data() + y * buffer.width * 3;
            
            for (int x = 0; x < buffer.width; ++x) {
                float r = HalfToFloat(srcRow[x * 4 + 0]);
                float g = HalfToFloat(srcRow[x * 4 + 1]);
                float b = HalfToFloat(srcRow[x * 4 + 2]);
                
                // SDR模式下直接钳制到0-1
                r = std::clamp(r, 0.0f, 1.0f);
                g = std::clamp(g, 0.0f, 1.0f);
                b = std::clamp(b, 0.0f, 1.0f);
                
                // 应用伽马校正
                r = LinearToSRGB(r);
                g = LinearToSRGB(g);
                b = LinearToSRGB(b);
                
                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
        
        // Update buffer
        buffer.format = PixelFormat::RGB8;
        buffer.stride = buffer.width * 3;
        buffer.data = std::move(rgbBuffer);
    }
    
    void PixelConvert::processHDR10(ImageBuffer& buffer, const Config* config) {
        const int pixelCount = buffer.width * buffer.height;
        std::vector<uint8_t> rgbBuffer(pixelCount * 3);
        
        float targetNits = config ? config->sdrBrightness : 250.0f;
        float maxNits = 1000.0f; // Default HDR content light level
        float exposure = targetNits / maxNits;
        
        for (int y = 0; y < buffer.height; ++y) {
            const auto* srcRow = reinterpret_cast<const uint32_t*>(buffer.data.data() + y * buffer.stride);
            auto* dstRow = rgbBuffer.data() + y * buffer.width * 3;
            
            for (int x = 0; x < buffer.width; ++x) {
                uint32_t pixel = srcRow[x];
                uint32_t r10 = (pixel >> 20) & 0x3FF;
                uint32_t g10 = (pixel >> 10) & 0x3FF;
                uint32_t b10 = pixel & 0x3FF;
                
                // PQ解码到线性光域（nits）
                float r = PQToLinear(static_cast<float>(r10) / 1023.0f) * exposure;
                float g = PQToLinear(static_cast<float>(g10) / 1023.0f) * exposure;
                float b = PQToLinear(static_cast<float>(b10) / 1023.0f) * exposure;
                
                // Rec.2020 到 sRGB 色域转换
                ColorSpace::Rec2020ToSRGB(r, g, b);
                
                // 非线性色调映射
                if (config && config->useACESFilmToneMapping) {
                    float rgb[3] = {r, g, b};
                    ToneMap_ACES(rgb, rgb, 1);
                    r = rgb[0]; g = rgb[1]; b = rgb[2];
                } else {
                    r = r / (1.0f + r);
                    g = g / (1.0f + g);
                    b = b / (1.0f + b);
                }
                
                // sRGB伽马校正
                r = LinearToSRGB(std::clamp(r, 0.0f, 1.0f));
                g = LinearToSRGB(std::clamp(g, 0.0f, 1.0f));
                b = LinearToSRGB(std::clamp(b, 0.0f, 1.0f));
                
                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
        
        // Update buffer
        buffer.format = PixelFormat::RGB8;
        buffer.stride = buffer.width * 3;
        buffer.data = std::move(rgbBuffer);
    }
    
    void PixelConvert::processSDR10(ImageBuffer& buffer) {
        const int pixelCount = buffer.width * buffer.height;
        std::vector<uint8_t> rgbBuffer(pixelCount * 3);
        
        for (int y = 0; y < buffer.height; ++y) {
            const auto* srcRow = reinterpret_cast<const uint32_t*>(buffer.data.data() + y * buffer.stride);
            auto* dstRow = rgbBuffer.data() + y * buffer.width * 3;
            
            for (int x = 0; x < buffer.width; ++x) {
                uint32_t pixel = srcRow[x];
                uint32_t r10 = (pixel >> 20) & 0x3FF;
                uint32_t g10 = (pixel >> 10) & 0x3FF;
                uint32_t b10 = pixel & 0x3FF;
                
                // SDR模式下简单缩放
                float r = static_cast<float>(r10) / 1023.0f;
                float g = static_cast<float>(g10) / 1023.0f;
                float b = static_cast<float>(b10) / 1023.0f;
                
                dstRow[x * 3 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
                dstRow[x * 3 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
                dstRow[x * 3 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            }
        }
        
        // Update buffer
        buffer.format = PixelFormat::RGB8;
        buffer.stride = buffer.width * 3;
        buffer.data = std::move(rgbBuffer);
    }
    
    void PixelConvert::processSDR(ImageBuffer& buffer) {
        const int pixelCount = buffer.width * buffer.height;
        std::vector<uint8_t> rgbBuffer(pixelCount * 3);
        
        for (int y = 0; y < buffer.height; ++y) {
            const auto* srcRow = buffer.data.data() + y * buffer.stride;
            auto* dstRow = rgbBuffer.data() + y * buffer.width * 3;
            
            for (int x = 0; x < buffer.width; ++x) {
                dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // R
                dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
                dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // B
            }
        }
        
        // Update buffer
        buffer.format = PixelFormat::RGB8;
        buffer.stride = buffer.width * 3;
        buffer.data = std::move(rgbBuffer);
    }

} // namespace screenshot_tool