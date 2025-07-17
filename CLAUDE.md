# HDR Screenshot Tool - Claude Documentation

## Project Overview

This is a Windows-based HDR (High Dynamic Range) screenshot tool written in C++20. The application captures screenshots from HDR displays and converts them to standard dynamic range (SDR) images using advanced tone mapping algorithms.

## Key Features

- **HDR Display Support**: Automatically detects HDR monitors and applies appropriate tone mapping
- **Dual Screenshot Modes**: Full screen and region selection
- **Smart Tone Mapping**: Supports Reinhard and ACES Film tone mapping algorithms  
- **Multiple Format Support**: Handles 16-bit float, 10-bit HDR, and traditional 8-bit formats
- **System Tray Integration**: Background operation with quick access
- **Customizable Hotkeys**: Configurable keyboard shortcuts
- **Auto-startup**: Optional system startup integration
- **Clipboard Integration**: Screenshots automatically copied to clipboard
- **File Saving**: Optional file output with customizable paths
- **Multi-monitor Support**: Works across multiple displays
- **Rotation-aware**: Handles rotated displays correctly

## Architecture

### Project Structure
```
src/
├── app/                    # Main application logic
│   ├── ScreenshotApp.cpp   # Core application class
│   ├── ScreenshotApp.hpp   # Application interface
│   └── WinMain.cpp         # Entry point
├── capture/                # Screenshot capture systems
│   ├── CaptureCommon.hpp   # Common capture definitions
│   ├── DXGICapture.*       # DirectX Graphics Infrastructure capture
│   ├── GDICapture.*        # Graphics Device Interface capture
│   └── SmartCapture.*      # Intelligent capture selection
├── config/                 # Configuration management
│   ├── Config.cpp          # Configuration implementation
│   └── Config.hpp          # Configuration structure
├── image/                  # Image processing and output
│   ├── ClipboardWriter.*   # Clipboard integration
│   ├── ColorSpace.*        # Color space conversions
│   ├── ImageBuffer.hpp     # Image data structures
│   ├── ImageSaverPNG.*     # PNG file output
│   ├── PixelConvert.*      # Pixel format conversions
│   └── ToneMapping.*       # HDR tone mapping algorithms
├── platform/               # Windows platform integration
│   ├── WinGDIPlusInit.*    # GDI+ initialization
│   ├── WinHeaders.hpp      # Windows headers
│   ├── WinNotification.*   # System notifications
│   └── WinShell.*          # Shell integration
├── ui/                     # User interface components
│   ├── HotkeyManager.*     # Global hotkey handling
│   ├── SelectionOverlay.*  # Screenshot region selection
│   └── TrayIcon.*          # System tray icon
└── util/                   # Utility functions
    ├── HotkeyParse.*       # Hotkey string parsing
    ├── Logger.*            # Debug logging
    ├── PathUtils.*         # File path utilities
    ├── ScopedWin.hpp       # RAII Windows resource wrappers
    ├── StringUtils.*       # String manipulation
    └── TimeUtils.*         # Time formatting utilities
```

## Core Components

### Main Application (`app/ScreenshotApp.*`)
- Central application controller
- Manages window message loop
- Coordinates all subsystems
- Handles hotkey events and tray menu actions

### Capture System (`capture/`)
- **SmartCapture**: Intelligently selects between DXGI and GDI capture methods
- **DXGICapture**: Hardware-accelerated capture using DirectX
- **GDICapture**: Fallback software capture method
- **CaptureCommon**: Shared definitions and result types

### Image Processing (`image/`)
- **ColorSpace**: Handles Rec.2020 to sRGB conversions
- **ToneMapping**: Implements Reinhard and ACES Film algorithms
- **PixelConvert**: Converts between different pixel formats
- **ClipboardWriter**: Manages clipboard operations
- **ImageSaverPNG**: Saves images to PNG files

### Configuration (`config/Config.*`)
Configuration structure includes:
- Hotkey definitions (default: Ctrl+Alt+A for region, Ctrl+Shift+Alt+A for fullscreen)
- Save path and file options
- Auto-start preferences
- HDR processing settings
- Multi-monitor behavior

## Build Configuration

### Requirements
- **Platform**: Windows 10 or higher
- **Compiler**: Visual Studio 2022 with v143 toolset
- **Language**: C++20 standard
- **Dependencies**: 
  - DirectX 11
  - DXGI (DirectX Graphics Infrastructure)
  - GDI+ (Windows Graphics Device Interface Plus)
  - Windows Shell API
  - COM (Component Object Model)

### Build Targets
- **Debug/Release**: Both x86 and x64 configurations
- **Subsystem**: Windows (GUI application, not console)
- **Character Set**: Unicode

## Development Guidelines

### Code Style
- Uses modern C++20 features
- RAII pattern for resource management (see `util/ScopedWin.hpp`)
- Namespace organization (`screenshot_tool`)
- Clear separation of concerns by module

### 🚨 Important: File Encoding Requirements
**CRITICAL**: All source files (.cpp/.hpp) must maintain UTF-8 encoding to preserve Chinese comments.

- **Always use UTF-8 (with or without BOM)** when editing files
- **Never use ANSI/GBK encoding** - this will corrupt Chinese comments
- When using tools like Claude Code, ensure Unicode support is enabled
- Chinese comments appear as "�" characters when encoding is corrupted
- If you see garbled Chinese text, the file encoding was damaged during editing

**Files with Chinese comments that require UTF-8:**
- All header files (.hpp) with Chinese documentation
- Implementation files (.cpp) with Chinese TODO comments
- Configuration and utility classes with Chinese explanations

### Common Tasks

#### Adding New Capture Methods
1. Implement interface in `capture/` directory
2. Register with `SmartCapture` selection logic
3. Handle platform-specific requirements

#### Extending Image Processing
1. Add new algorithms to `image/ToneMapping.*`
2. Update `image/PixelConvert.*` for format support
3. Modify configuration if user-selectable

#### Adding UI Features
1. Extend `ui/TrayIcon.*` for menu items
2. Update `ui/HotkeyManager.*` for new shortcuts
3. Modify `config/Config.*` for persistence

### Testing
- Enable debug mode via `config.ini`: `DebugMode=true`
- Debug output written to `debug.txt`
- Use Visual Studio debugger for development

### Configuration Management
- Settings stored in `config.ini` (INI format)
- Auto-generated on first run with defaults
- Supports runtime reload via tray menu
- See `config/Config.hpp` for all available options

## HDR Processing Technical Details

### Supported Formats
- **R16G16B16A16_FLOAT**: 16-bit floating point HDR
- **R10G10B10A2_UNORM**: 10-bit HDR (HDR10)
- **Traditional BGRA**: 8-bit SDR fallback

### Tone Mapping Algorithms
- **Reinhard** (default): `tone_mapped = x / (1 + x)`
- **ACES Film**: Cinematic tone mapping for high contrast scenes

### Color Space Handling
- Automatic Rec.2020 → sRGB conversion for HDR10 content
- Linear light → sRGB gamma correction for final output

## Common Development Patterns

### Error Handling
- Use `CaptureResult` enum for capture operation status
- Automatic retry logic for temporary failures
- Graceful fallback between capture methods

### Resource Management
- RAII wrappers in `util/ScopedWin.hpp`
- Automatic COM object lifecycle management
- Memory leak detection in debug builds

### Platform Integration
- Windows message handling in main application loop
- System tray notifications and context menus
- Registry integration for auto-startup

## Build Commands

```bash
# Build Debug x64 (recommended)
msbuild HDR_Screenshot_Tool.sln /p:Configuration=Debug /p:Platform=x64

# Build Release x64
msbuild HDR_Screenshot_Tool.sln /p:Configuration=Release /p:Platform=x64

# Clean solution
msbuild HDR_Screenshot_Tool.sln /t:Clean
```

## Debugging Tips

1. Enable debug mode in `config.ini`
2. Check `debug.txt` for detailed logging
3. Use Visual Studio debugger for breakpoints
4. Monitor Windows Event Viewer for system-level issues
5. Test with different display configurations (SDR/HDR)

## Troubleshooting

### File Encoding Issues
If you encounter build errors or see garbled Chinese text:

1. **Check file encoding**: Ensure all .cpp/.hpp files are UTF-8
2. **Fix corrupted comments**: Look for "�" characters in comments
3. **Use proper editors**: Visual Studio, VS Code with UTF-8 support
4. **Verify BOM**: UTF-8 with BOM is recommended for Windows development

### Common Encoding Problems
- **Symptom**: Chinese comments show as "������"
- **Cause**: File was saved in ANSI/GBK instead of UTF-8
- **Fix**: Re-save file in UTF-8 encoding and restore Chinese comments

### Class Structure Issues
All utility classes should follow this pattern:
```cpp
namespace screenshot_tool {
    class UtilityClass {
    public:
        static ReturnType MethodName(parameters...);
    };
}
```

This tool is designed as a defensive security application for capturing and analyzing display content, with no malicious capabilities.