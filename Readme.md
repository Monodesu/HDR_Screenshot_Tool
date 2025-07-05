# HDR Screenshot Tool

一个专为Windows系统设计的高动态范围(HDR)截图工具，能够正确处理HDR显示器的截图并将其转换为标准动态范围(SDR)图像。

## 功能特性

- **HDR显示支持**: 自动检测HDR显示器并应用适当的色调映射
- **双截图模式**: 支持全屏截图和区域选择截图
- **智能色调映射**: 支持Reinhard和ACES Film色调映射算法
- **多种格式支持**: 自动处理16位浮点、10位HDR和传统8位格式
- **系统托盘集成**: 后台运行，通过系统托盘快速访问
- **自定义热键**: 可配置的快捷键组合
- **自动启动**: 支持开机自启动
- **剪贴板集成**: 截图自动复制到剪贴板
- **文件保存**: 可选择同时保存到文件
- **可视化选择**: 带有实时尺寸显示的选择框

## 系统要求

- Windows 10 或更高版本
- 支持DirectX 11的显卡
- Visual Studio 2022 (用于编译)
- Windows SDK 10.0

## 安装说明

### 从源代码编译

1. **克隆或下载项目**
   ```bash
   git clone <repository-url>
   cd HDR_Screenshot_Tool
   ```

2. **使用Visual Studio编译**
   - 打开 `HDR_Screenshot_Tool.sln`
   - 选择 `Release|x64` 配置
   - 点击"生成" → "生成解决方案"

3. **运行程序**
   - 编译完成后，在 `x64/Release/` 目录找到 `HDR_Screenshot_Tool.exe`
   - 双击运行程序

## 使用说明

### 首次运行

1. 运行 `HDR_Screenshot_Tool.exe`
2. 程序将在系统托盘中显示图标
3. 首次运行会自动创建 `config.ini` 配置文件

### 基本操作

#### 区域截图
- **默认热键**: `Ctrl + Alt + A`
- 按下热键后屏幕会显示半透明覆盖层
- 按住鼠标左键拖拽选择区域
- 释放鼠标完成截图
- 按 `Esc` 或鼠标右键取消

#### 全屏截图
- **默认热键**: `Ctrl + Shift + Alt + A`
- 按下热键立即截取整个屏幕

#### 系统托盘菜单
右键点击托盘图标可以访问：
- **开机启动**: 切换开机自启动
- **保存到文件**: 切换是否保存截图到文件
- **重载配置**: 重新加载配置文件
- **退出**: 关闭程序

### 配置选项

程序会在同目录下创建 `config.ini` 文件，可以手动编辑以下选项：

```ini
; HDR Screenshot Tool Configuration
; Basic hotkeys and paths
RegionHotkey=ctrl+alt+a
FullscreenHotkey=ctrl+shift+alt+a
SavePath=Screenshots
AutoStart=false
SaveToFile=true
ShowNotification=true

; Debug settings
DebugMode=false

; HDR settings
UseACESFilmToneMapping=false
SDRBrightness=250.0
```

#### 配置说明

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `RegionHotkey` | 区域截图热键 | `ctrl+alt+a` |
| `FullscreenHotkey` | 全屏截图热键 | `ctrl+shift+alt+a` |
| `SavePath` | 截图保存路径 | `Screenshots` |
| `AutoStart` | 开机启动 | `false` |
| `SaveToFile` | 保存到文件 | `true` |
| `ShowNotification` | 显示通知 | `true` |
| `DebugMode` | 调试模式 | `false` |
| `UseACESFilmToneMapping` | 使用ACES色调映射 | `false` |
| `SDRBrightness` | SDR目标亮度(nits) | `250.0` |

### 热键格式

热键支持以下修饰键组合：
- `ctrl` - Ctrl键
- `shift` - Shift键  
- `alt` - Alt键

支持的字母键：
- `a`, `s`, `d` 等

示例：
- `ctrl+a` - Ctrl + A
- `ctrl+shift+s` - Ctrl + Shift + S
- `ctrl+alt+shift+d` - Ctrl + Alt + Shift + D

## HDR处理技术

### 支持的格式
- **R16G16B16A16_FLOAT**: 16位浮点HDR格式
- **R10G10B10A2_UNORM**: 10位HDR格式 (HDR10)
- **传统BGRA格式**: 8位SDR格式

### 色调映射算法

#### Reinhard色调映射 (默认)
```
tone_mapped = x / (1 + x)
```
适合大多数HDR内容，保持自然的视觉效果。

#### ACES Film色调映射
```
tone_mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)
```
提供更电影化的效果，适合高对比度场景。

### 色域转换

程序自动处理以下色域转换：
- **Rec.2020** → **sRGB** (用于HDR10内容)
- **线性光域** → **sRGB gamma** (最终输出)

## 故障排除

### 常见问题

**Q: 程序无法启动**
- 确保系统支持DirectX 11
- 检查是否有其他实例正在运行
- 以管理员权限运行

**Q: HDR截图效果不正确**
- 确认显示器已启用HDR模式
- 调整 `SDRBrightness` 参数
- 尝试切换色调映射算法

**Q: 热键不工作**
- 检查是否与其他程序冲突
- 修改 `config.ini` 中的热键设置
- 重载配置或重启程序

**Q: 截图保存失败**
- 检查 `SavePath` 目录权限
- 确保磁盘空间充足
- 检查文件名是否包含非法字符

### 调试模式

如果遇到问题，可以启用调试模式：

1. 在 `config.ini` 中设置 `DebugMode=true`
2. 重载配置或重启程序
3. 程序会在同目录生成 `debug.txt` 文件
4. 查看调试信息以诊断问题

### 性能优化

- 在低端设备上，选择较小的截图区域
- 关闭不必要的通知 (`ShowNotification=false`)
- 如果不需要文件保存，设置 `SaveToFile=false`

## 开发信息

### 编译依赖

- **Direct3D 11**: 图形接口
- **DXGI**: 显示器管理和截图
- **GDI+**: 图像处理和保存
- **Windows Shell API**: 系统托盘和快捷方式
- **COM**: 组件对象模型接口

### 项目结构

```
HDR_Screenshot_Tool/
├── HDR_Screenshot_Tool.cpp    # 主程序源代码
├── HDR_Screenshot_Tool.vcxproj # Visual Studio项目文件
├── HDR_Screenshot_Tool.sln    # Visual Studio解决方案文件
├── config.ini                # 配置文件 (运行时生成)
├── Screenshots/               # 默认截图保存目录
└── debug.txt                 # 调试日志 (调试模式下生成)
```

### 技术特点

- **现代C++**: 使用C++20标准特性
- **RAII内存管理**: 自动资源管理
- **COM智能指针**: 安全的COM对象管理
- **异常安全**: 完整的错误处理机制

## 许可证

本项目采用开源许可证，具体条款请查看 LICENSE 文件。

## 更新日志

### v1.0.0
- 初始发布
- 支持HDR和SDR截图
- 实现区域选择和全屏截图
- 添加系统托盘集成
- 支持自定义热键和配置

## 反馈与支持

如果您遇到问题或有改进建议，请：

1. 启用调试模式收集日志信息
2. 创建Issue并附上详细描述
3. 包含系统信息和配置文件内容

感谢您使用HDR Screenshot Tool！