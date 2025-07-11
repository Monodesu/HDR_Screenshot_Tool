#pragma once
#include "Config.hpp"
#include "common.hpp"

class HDRScreenCapture {
public:
	// 绘制注释的结构
	struct DrawAnnotation {
		enum Type { Arrow, Line, Rectangle, Circle } type;
		POINT start, end;
		COLORREF color;
		int thickness;
	};

private:
	struct MonitorInfo {
		ComPtr<IDXGIAdapter1> adapter;
		ComPtr<IDXGIOutput6> output6;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<IDXGIOutputDuplication> dupl;
		RECT desktopRect{};
		UINT width = 0;
		UINT height = 0;
		DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY;
	};

	bool InitMonitor(MonitorInfo& info) {
		D3D_FEATURE_LEVEL fl;
		HRESULT hr = D3D11CreateDevice(
			info.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
			0, nullptr, 0, D3D11_SDK_VERSION,
			&info.device, &fl, &info.context);
		if (FAILED(hr)) return false;

		DXGI_FORMAT fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
		hr = info.output6->DuplicateOutput1(info.device.Get(), 0, 1, &fmt, &info.dupl);
		if (FAILED(hr)) {
			hr = info.output6->DuplicateOutput(info.device.Get(), &info.dupl);
		}
		if (FAILED(hr)) return false;

		DXGI_OUTDUPL_DESC dd{};
		info.dupl->GetDesc(&dd);
		info.rotation = dd.Rotation;
		info.width = dd.ModeDesc.Width;
		info.height = dd.ModeDesc.Height;

		return true;
	}

	std::vector<MonitorInfo> monitors;
	int virtualLeft = 0;
	int virtualTop = 0;
	int virtualWidth = 0;
	int virtualHeight = 0;
	bool isHDREnabled = false;
	const Config* config = nullptr; // 添加配置引用

	// HDR元数据结构
	struct HDRMetadata {
		float maxLuminance = 1000.0f;
		float minLuminance = 0.1f;
		float maxContentLightLevel = 1000.0f;
	};

public:
	void SetConfig(const Config* cfg) { config = cfg; }
	const std::vector<MonitorInfo>& GetMonitors() const { return monitors; }
	int GetVirtualLeft() const { return virtualLeft; }
	int GetVirtualTop() const { return virtualTop; }

	bool Reinitialize() {
		monitors.clear();
		virtualLeft = virtualTop = 0;
		virtualWidth = virtualHeight = 0;
		return Initialize();
	}

	bool Initialize() {
		ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

		UINT adapterIndex = 0;
		RECT virtualRect{ INT_MAX, INT_MAX, INT_MIN, INT_MIN };
		while (true) {
			ComPtr<IDXGIAdapter1> adapter;
			if (FAILED(factory->EnumAdapters1(adapterIndex, &adapter))) break;

			UINT outputIndex = 0;
			while (true) {
				ComPtr<IDXGIOutput> output;
				if (FAILED(adapter->EnumOutputs(outputIndex, &output))) break;

				MonitorInfo info{};
				info.adapter = adapter;
				if (FAILED(output.As(&info.output6))) { ++outputIndex; continue; }

				DXGI_OUTPUT_DESC desc;
				info.output6->GetDesc(&desc);
				info.desktopRect = desc.DesktopCoordinates;

				virtualRect.left = std::min(virtualRect.left, desc.DesktopCoordinates.left);
				virtualRect.top = std::min(virtualRect.top, desc.DesktopCoordinates.top);
				virtualRect.right = std::max(virtualRect.right, desc.DesktopCoordinates.right);
				virtualRect.bottom = std::max(virtualRect.bottom, desc.DesktopCoordinates.bottom);

				if (InitMonitor(info)) {
					monitors.push_back(std::move(info));
				}

				++outputIndex;
			}
			++adapterIndex;
		}

		if (monitors.empty()) return false;

		virtualLeft = virtualRect.left;
		virtualTop = virtualRect.top;
		virtualWidth = virtualRect.right - virtualRect.left;
		virtualHeight = virtualRect.bottom - virtualRect.top;

		// 检测HDR状态(基于首个显示器)
		DetectHDRStatus();

		return true;
	}

	// 公共方法：直接保存RGB数据到文件和剪贴板
	bool SaveRGBData(const std::vector<uint8_t>& rgbData, int width, int height,
		const std::string& filename = "", const std::vector<DrawAnnotation>* annotations = nullptr) {

		std::vector<uint8_t> buffer = rgbData;

		// 如果有绘制注释，添加到图像上
		if (annotations && !annotations->empty()) {
			ApplyAnnotationsToRGB(buffer, width, height, *annotations);
		}
		bool clipboardSuccess = SaveToClipboard(buffer, width, height);

		bool fileSuccess = true;
		if (!filename.empty()) {
			fileSuccess = SavePNG(buffer, width, height, filename);
		}

		return clipboardSuccess && fileSuccess;
	}

	bool CaptureRegion(int x, int y, int width, int height, const std::string& filename = "",
		const std::vector<DrawAnnotation>* annotations = nullptr) {
		RECT regionRect{ x, y, x + width, y + height };

		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		UINT bpp = 0;
		std::vector<uint8_t> buffer;
		bool gotFrame = false;
		bool allSuccess = true;

		for (auto& m : monitors) {
			RECT inter{};
			if (!IntersectRect(&inter, &regionRect, &m.desktopRect)) continue;

			ComPtr<IDXGIResource> resource;
			DXGI_OUTDUPL_FRAME_INFO frameInfo;
			HRESULT hr = m.dupl->AcquireNextFrame(100, &frameInfo, &resource);
			if (FAILED(hr)) {
				if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED) {
					InitMonitor(m);
				}
				hr = m.dupl->AcquireNextFrame(100, &frameInfo, &resource);
			}
			if (FAILED(hr)) { allSuccess = false; continue; }
			gotFrame = true;

			auto cleanup = [&](void*) { m.dupl->ReleaseFrame(); };
			std::unique_ptr<void, decltype(cleanup)> frameGuard(reinterpret_cast<void*>(1), cleanup);

			ComPtr<ID3D11Texture2D> texture;
			if (FAILED(resource.As(&texture))) continue;

			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			if (format == DXGI_FORMAT_UNKNOWN) {
				format = desc.Format;
				bpp = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
				buffer.resize(width * height * bpp);
			}

			D3D11_TEXTURE2D_DESC stagingDesc = desc;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.BindFlags = 0;
			stagingDesc.MiscFlags = 0;
			ComPtr<ID3D11Texture2D> staging;
			if (FAILED(m.device->CreateTexture2D(&stagingDesc, nullptr, &staging))) continue;
			m.context->CopyResource(staging.Get(), texture.Get());

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(m.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;

			auto unmap = [&](void*) { m.context->Unmap(staging.Get(), 0); };
			std::unique_ptr<void, decltype(unmap)> mapGuard(reinterpret_cast<void*>(1), unmap);

			int destX = inter.left - x;
			int destY = inter.top - y;
			int rw = inter.right - inter.left;
			int rh = inter.bottom - inter.top;

			int rx0 = inter.left - m.desktopRect.left;
			int ry0 = inter.top - m.desktopRect.top;

			for (int row = 0; row < rh; ++row) {
				for (int col = 0; col < rw; ++col) {
					int rx = rx0 + col;
					int ry = ry0 + row;

					int u = rx, v = ry;
					switch (m.rotation) {
					case DXGI_MODE_ROTATION_ROTATE90:
						u = ry;
						v = static_cast<int>(m.width) - rx - 1;
						break;
					case DXGI_MODE_ROTATION_ROTATE180:
						u = static_cast<int>(m.width) - rx - 1;
						v = static_cast<int>(m.height) - ry - 1;
						break;
					case DXGI_MODE_ROTATION_ROTATE270:
						u = static_cast<int>(m.height) - ry - 1;
						v = rx;
						break;
					default:
						break;
					}

					const uint8_t* src = static_cast<const uint8_t*>(mapped.pData) + v * mapped.RowPitch + u * bpp;
					uint8_t* dst = buffer.data() + ((destY + row) * width + destX + col) * bpp;
					memcpy(dst, src, bpp);
				}
			}
		}

		if (!gotFrame || !allSuccess ||
			std::all_of(buffer.begin(), buffer.end(), [](uint8_t v) { return v == 0; })) {
			return false;
		}

		return ProcessAndSave(buffer.data(), width, height, width * bpp, format, filename, annotations);
	}

	bool CaptureFullscreen(const std::string& filename = "") {
		return CaptureRegion(virtualLeft, virtualTop, virtualWidth, virtualHeight, filename);
	}

private:
	void DetectHDRStatus() {
		// 检测显示器是否支持HDR并已启用
		DXGI_OUTPUT_DESC1 outputDesc1;
		isHDREnabled = false; // 默认为false

		if (!monitors.empty()) {
			ComPtr<IDXGIOutput6> output6Temp;
			if (SUCCEEDED(monitors.front().output6.As(&output6Temp))) {
				if (SUCCEEDED(output6Temp->GetDesc1(&outputDesc1))) {
					if (GetDebugMode()) {
						std::ofstream debug("debug.txt", std::ios::app);
						debug << "Monitor Info:" << std::endl;
						debug << "ColorSpace: " << static_cast<int>(outputDesc1.ColorSpace) << std::endl;
						debug << "MaxLuminance: " << outputDesc1.MaxLuminance << std::endl;
						debug << "MinLuminance: " << outputDesc1.MinLuminance << std::endl;
						debug << "MaxFullFrameLuminance: " << outputDesc1.MaxFullFrameLuminance << std::endl;
					}

					// Windows HDR模式的颜色空间检查
					// DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12
					// DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 = 1
					isHDREnabled = (outputDesc1.ColorSpace == 12) || // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
						(outputDesc1.ColorSpace == 1);    // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709

					if (GetDebugMode()) {
						std::ofstream debug("debug.txt", std::ios::app);
						debug << "HDR detection based on ColorSpace: " << (isHDREnabled ? "Yes" : "No") << std::endl;
						debug.close();
					}

					// 额外检查：如果MaxLuminance > 80，也认为是HDR
					if (!isHDREnabled && outputDesc1.MaxLuminance > 80.0f) {
						isHDREnabled = true;
						if (GetDebugMode()) {
							std::ofstream debug("debug.txt", std::ios::app);
							debug << "HDR detection based on MaxLuminance: Yes" << std::endl;
							debug.close();
						}
					}

					// 强制检测：暂时假设任何非默认设置都是HDR
					if (!isHDREnabled) {
						isHDREnabled = true; // 强制启用HDR处理进行测试
						if (GetDebugMode()) {
							std::ofstream debug("debug.txt", std::ios::app);
							debug << "Force HDR processing to be enabled for testing" << std::endl;
							debug.close();
						}
					}

					if (GetDebugMode()) {
						std::ofstream debug("debug.txt", std::ios::app);
						debug << "HDR Status: " << (isHDREnabled ? "Enabled" : "Disabled") << std::endl;
						debug.close();
					}
				}
			}
		}

		// 如果无法获取信息，默认启用HDR处理
		if (!isHDREnabled) {
			isHDREnabled = true;
			if (GetDebugMode()) {
				std::ofstream debug("debug.txt", std::ios::app);
				debug << "Unable to obtain display information, HDR processing is enabled by default" << std::endl;
				debug.close();
			}
		}
	}

	HDRMetadata GetDisplayHDRMetadata() {
		HDRMetadata metadata;

		if (!monitors.empty()) {
			ComPtr<IDXGIOutput6> output6Temp;
			if (SUCCEEDED(monitors.front().output6.As(&output6Temp))) {
				DXGI_OUTPUT_DESC1 outputDesc1;
				if (SUCCEEDED(output6Temp->GetDesc1(&outputDesc1))) {
					metadata.maxLuminance = outputDesc1.MaxLuminance;
					metadata.minLuminance = outputDesc1.MinLuminance;
					metadata.maxContentLightLevel = outputDesc1.MaxFullFrameLuminance;
				}
			}
		}

		return metadata;
	}

	bool ProcessAndSave(uint8_t* data, int width, int height, int pitch,
		DXGI_FORMAT format, const std::string& filename,
		const std::vector<DrawAnnotation>* annotations = nullptr) {

		std::vector<uint8_t> rgbBuffer(width * height * 3);

		// 添加调试信息
		static bool debugOnce = true;
		if (GetDebugMode() && debugOnce) {
			std::ofstream debug("debug.txt", std::ios::app);
			debug << "DXGI Format: " << static_cast<int>(format) << std::endl;
			debug << "HDR Detected: " << (isHDREnabled ? "Yes" : "No") << std::endl;
			debug << "Handling branches: ";
			debugOnce = false;

			// 根据格式和HDR状态处理数据
			switch (format) {
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
				debug << "R16G16B16A16_FLOAT";
				if (isHDREnabled) {
					debug << " -> HDR Processing" << std::endl;
					ProcessHDR16Float(data, rgbBuffer.data(), width, height, pitch);
				}
				else {
					debug << " -> SDR Processing" << std::endl;
					ProcessSDR16Float(data, rgbBuffer.data(), width, height, pitch);
				}
				break;
			case DXGI_FORMAT_R10G10B10A2_UNORM:
				debug << "R10G10B10A2_UNORM";
				if (isHDREnabled) {
					debug << " -> HDR Processing" << std::endl;
					ProcessHDR10(data, rgbBuffer.data(), width, height, pitch);
				}
				else {
					debug << " -> SDR Processing" << std::endl;
					ProcessSDR10(data, rgbBuffer.data(), width, height, pitch);
				}
				break;
			default:
				debug << "Other format(" << static_cast<int>(format) << ") -> SDR Processing" << std::endl;
				ProcessSDR(data, rgbBuffer.data(), width, height, pitch);
				break;
			}
			debug.close();
		}
		else {
			// 正常处理分支
			switch (format) {
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
				if (isHDREnabled) {
					ProcessHDR16Float(data, rgbBuffer.data(), width, height, pitch);
				}
				else {
					ProcessSDR16Float(data, rgbBuffer.data(), width, height, pitch);
				}
				break;
			case DXGI_FORMAT_R10G10B10A2_UNORM:
				if (isHDREnabled) {
					ProcessHDR10(data, rgbBuffer.data(), width, height, pitch);
				}
				else {
					ProcessSDR10(data, rgbBuffer.data(), width, height, pitch);
				}
				break;
			default:
				ProcessSDR(data, rgbBuffer.data(), width, height, pitch);
				break;
			}
		}

		// 如果有绘制注释，添加到图像上
		if (annotations && !annotations->empty()) {
			ApplyAnnotations(rgbBuffer, width, height, *annotations);
		}

		// 保存到剪贴板
		bool clipboardSuccess = SaveToClipboard(rgbBuffer, width, height);

		// 如果需要保存到文件
		bool fileSuccess = true;
		if (!filename.empty()) {
			fileSuccess = SavePNG(rgbBuffer, width, height, filename);
		}

		return clipboardSuccess && fileSuccess;
	}

	// 将注释直接应用到RGB缓冲区
	void ApplyAnnotationsToRGB(std::vector<uint8_t>& rgbBuffer, int width, int height,
		const std::vector<DrawAnnotation>& annotations) {

		// 创建一个临时位图来绘制注释
		HDC hdc = GetDC(nullptr);
		HDC memDC = CreateCompatibleDC(hdc);
		HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
		HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

		// 将RGB数据复制到位图
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height; // 负数表示从上到下
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 24;
		bmi.bmiHeader.biCompression = BI_RGB;

		// 转换RGB到BGR格式用于SetDIBits
		std::vector<uint8_t> bgrBuffer(width * height * 3);
		for (int i = 0; i < width * height; i++) {
			bgrBuffer[i * 3 + 0] = rgbBuffer[i * 3 + 2]; // B
			bgrBuffer[i * 3 + 1] = rgbBuffer[i * 3 + 1]; // G
			bgrBuffer[i * 3 + 2] = rgbBuffer[i * 3 + 0]; // R
		}

		SetDIBits(memDC, bitmap, 0, height, bgrBuffer.data(), &bmi, DIB_RGB_COLORS);

		// 绘制注释
		for (const auto& annotation : annotations) {
			HPEN pen = CreatePen(PS_SOLID, annotation.thickness, annotation.color);
			HPEN oldPen = (HPEN)SelectObject(memDC, pen);
			HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));

			switch (annotation.type) {
			case DrawAnnotation::Line:
				MoveToEx(memDC, annotation.start.x, annotation.start.y, nullptr);
				LineTo(memDC, annotation.end.x, annotation.end.y);
				break;

			case DrawAnnotation::Rectangle:
				Rectangle(memDC, annotation.start.x, annotation.start.y,
					annotation.end.x, annotation.end.y);
				break;

			case DrawAnnotation::Circle: {
				int radius = static_cast<int>(sqrt(pow(annotation.end.x - annotation.start.x, 2) +
					pow(annotation.end.y - annotation.start.y, 2)));
				Ellipse(memDC, annotation.start.x - radius, annotation.start.y - radius,
					annotation.start.x + radius, annotation.start.y + radius);
				break;
			}

			case DrawAnnotation::Arrow: {
				// 绘制箭头主体
				MoveToEx(memDC, annotation.start.x, annotation.start.y, nullptr);
				LineTo(memDC, annotation.end.x, annotation.end.y);

				// 计算箭头头部
				double angle = atan2(annotation.end.y - annotation.start.y,
					annotation.end.x - annotation.start.x);
				int arrowLength = 15;
				double arrowAngle = 0.5;

				int x1 = annotation.end.x - arrowLength * cos(angle - arrowAngle);
				int y1 = annotation.end.y - arrowLength * sin(angle - arrowAngle);
				int x2 = annotation.end.x - arrowLength * cos(angle + arrowAngle);
				int y2 = annotation.end.y - arrowLength * sin(angle + arrowAngle);

				MoveToEx(memDC, annotation.end.x, annotation.end.y, nullptr);
				LineTo(memDC, x1, y1);
				MoveToEx(memDC, annotation.end.x, annotation.end.y, nullptr);
				LineTo(memDC, x2, y2);
				break;
			}
			}

			SelectObject(memDC, oldPen);
			SelectObject(memDC, oldBrush);
			DeleteObject(pen);
		}

		// 将绘制结果读回RGB缓冲区
		GetDIBits(memDC, bitmap, 0, height, bgrBuffer.data(), &bmi, DIB_RGB_COLORS);

		// 转换回RGB格式
		for (int i = 0; i < width * height; i++) {
			rgbBuffer[i * 3 + 0] = bgrBuffer[i * 3 + 2]; // R
			rgbBuffer[i * 3 + 1] = bgrBuffer[i * 3 + 1]; // G
			rgbBuffer[i * 3 + 2] = bgrBuffer[i * 3 + 0]; // B
		}

		SelectObject(memDC, oldBitmap);
		DeleteObject(bitmap);
		DeleteDC(memDC);
		ReleaseDC(nullptr, hdc);
	}

	void ApplyAnnotations(std::vector<uint8_t>& rgbBuffer, int width, int height,
		const std::vector<DrawAnnotation>& annotations) {

		// 创建一个临时位图来绘制注释
		HDC hdc = GetDC(nullptr);
		HDC memDC = CreateCompatibleDC(hdc);
		HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
		HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

		// 将RGB数据复制到位图
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height; // 负数表示从上到下
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 24;
		bmi.bmiHeader.biCompression = BI_RGB;

		// 转换RGB到BGR格式用于SetDIBits
		std::vector<uint8_t> bgrBuffer(width * height * 3);
		for (int i = 0; i < width * height; i++) {
			bgrBuffer[i * 3 + 0] = rgbBuffer[i * 3 + 2]; // B
			bgrBuffer[i * 3 + 1] = rgbBuffer[i * 3 + 1]; // G
			bgrBuffer[i * 3 + 2] = rgbBuffer[i * 3 + 0]; // R
		}

		SetDIBits(memDC, bitmap, 0, height, bgrBuffer.data(), &bmi, DIB_RGB_COLORS);

		// 绘制注释
		for (const auto& annotation : annotations) {
			HPEN pen = CreatePen(PS_SOLID, annotation.thickness, annotation.color);
			HPEN oldPen = (HPEN)SelectObject(memDC, pen);
			HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));

			switch (annotation.type) {
			case DrawAnnotation::Line:
				MoveToEx(memDC, annotation.start.x, annotation.start.y, nullptr);
				LineTo(memDC, annotation.end.x, annotation.end.y);
				break;

			case DrawAnnotation::Rectangle:
				Rectangle(memDC, annotation.start.x, annotation.start.y,
					annotation.end.x, annotation.end.y);
				break;

			case DrawAnnotation::Circle: {
				int radius = static_cast<int>(sqrt(pow(annotation.end.x - annotation.start.x, 2) +
					pow(annotation.end.y - annotation.start.y, 2)));
				Ellipse(memDC, annotation.start.x - radius, annotation.start.y - radius,
					annotation.start.x + radius, annotation.start.y + radius);
				break;
			}

			case DrawAnnotation::Arrow: {
				// 绘制箭头主体
				MoveToEx(memDC, annotation.start.x, annotation.start.y, nullptr);
				LineTo(memDC, annotation.end.x, annotation.end.y);

				// 计算箭头头部
				double angle = atan2(annotation.end.y - annotation.start.y,
					annotation.end.x - annotation.start.x);
				int arrowLength = 15;
				double arrowAngle = 0.5;

				int x1 = annotation.end.x - arrowLength * cos(angle - arrowAngle);
				int y1 = annotation.end.y - arrowLength * sin(angle - arrowAngle);
				int x2 = annotation.end.x - arrowLength * cos(angle + arrowAngle);
				int y2 = annotation.end.y - arrowLength * sin(angle + arrowAngle);

				MoveToEx(memDC, annotation.end.x, annotation.end.y, nullptr);
				LineTo(memDC, x1, y1);
				MoveToEx(memDC, annotation.end.x, annotation.end.y, nullptr);
				LineTo(memDC, x2, y2);
				break;
			}
			}

			SelectObject(memDC, oldPen);
			SelectObject(memDC, oldBrush);
			DeleteObject(pen);
		}

		// 将绘制结果读回RGB缓冲区
		GetDIBits(memDC, bitmap, 0, height, bgrBuffer.data(), &bmi, DIB_RGB_COLORS);

		// 转换回RGB格式
		for (int i = 0; i < width* height; i++) {
			rgbBuffer[i * 3 + 0] = bgrBuffer[i * 3 + 2]; // R
			rgbBuffer[i * 3 + 1] = bgrBuffer[i * 3 + 1]; // G
			rgbBuffer[i * 3 + 2] = bgrBuffer[i * 3 + 0]; // B
		}

		SelectObject(memDC, oldBitmap);
		DeleteObject(bitmap);
		DeleteDC(memDC);
		ReleaseDC(nullptr, hdc);
	}

	void ProcessHDR16Float(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
		// 获取当前显示器 HDR 最大亮度
		auto meta = GetDisplayHDRMetadata();
		float maxNits = meta.maxLuminance > 0.0f ? meta.maxLuminance : 1000.0f;

		// 目标 SDR 显示亮度
		float targetNits = config ? config->sdrBrightness : 250.0f;

		// 计算曝光系数
		float exposure = targetNits / maxNits;

		for (int y : std::views::iota(0, height)) {
			auto* srcRow = reinterpret_cast<uint16_t*>(src + y * pitch);
			auto* dstRow = dst + y * width * 3;

			for (int x : std::views::iota(0, width)) {
				float r = HalfToFloat(srcRow[x * 4 + 0]) * exposure;
				float g = HalfToFloat(srcRow[x * 4 + 1]) * exposure;
				float b = HalfToFloat(srcRow[x * 4 + 2]) * exposure;

				// 色调映射
				if (config && config->useACESFilmToneMapping) {
					r = ACESFilmToneMapping(r);
					g = ACESFilmToneMapping(g);
					b = ACESFilmToneMapping(b);
				}
				else {
					r = ReinhardToneMapping(r);
					g = ReinhardToneMapping(g);
					b = ReinhardToneMapping(b);
				}

				// sRGB 伽马校正
				r = LinearToSRGB(r);
				g = LinearToSRGB(g);
				b = LinearToSRGB(b);

				// 写入 RGB 输出
				dstRow[x * 3 + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
				dstRow[x * 3 + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
				dstRow[x * 3 + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
			}
		}
	}

	bool GetDebugMode() const {
		return config ? config->debugMode : false;
	}

	void ProcessHDR10(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
		// HDR10格式使用PQ编码和Rec.2020色域

		for (int y : std::views::iota(0, height)) {
			auto* srcRow = reinterpret_cast<uint32_t*>(src + y * pitch);
			auto* dstRow = dst + y * width * 3;

			for (int x : std::views::iota(0, width)) {
				uint32_t pixel = srcRow[x];
				uint32_t r10 = (pixel >> 20) & 0x3FF;
				uint32_t g10 = (pixel >> 10) & 0x3FF;
				uint32_t b10 = pixel & 0x3FF;

				// PQ解码到线性光域（nits）
				float r = PQToLinear(static_cast<float>(r10) / 1023.0f);
				float g = PQToLinear(static_cast<float>(g10) / 1023.0f);
				float b = PQToLinear(static_cast<float>(b10) / 1023.0f);

				auto meta = GetDisplayHDRMetadata();
				float maxNits = meta.maxContentLightLevel > 0.0f ? meta.maxContentLightLevel : 1000.0f;
				float targetNits = config ? config->sdrBrightness : 250.0f;
				float exposure = targetNits / maxNits;
				r = r * exposure;
				g = g * exposure;
				b = b * exposure;

				// Rec.2020 到 sRGB 色域转换
				Rec2020ToSRGB(r, g, b);

				// 非线性色调映射
				if (config && config->useACESFilmToneMapping) {
					r = ACESFilmToneMapping(r);
					g = ACESFilmToneMapping(g);
					b = ACESFilmToneMapping(b);
				}
				else {
					r = ReinhardToneMapping(r);
					g = ReinhardToneMapping(g);
					b = ReinhardToneMapping(b);
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
	}

	void ProcessSDR16Float(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
		// 添加调试输出
		static bool debugOnce = true;
		if (GetDebugMode() && debugOnce) {
			std::ofstream debug("debug.txt", std::ios::app);
			debug << "调用了 ProcessSDR16Float" << std::endl;
			debugOnce = false;
		}

		for (int y : std::views::iota(0, height)) {
			auto* srcRow = reinterpret_cast<uint16_t*>(src + y * pitch);
			auto* dstRow = dst + y * width * 3;

			for (int x : std::views::iota(0, width)) {
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
	}

	void ProcessSDR10(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
		for (int y : std::views::iota(0, height)) {
			auto* srcRow = reinterpret_cast<uint32_t*>(src + y * pitch);
			auto* dstRow = dst + y * width * 3;

			for (int x : std::views::iota(0, width)) {
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

	void ProcessSDR(uint8_t* src, uint8_t* dst, int width, int height, int pitch) {
		// 添加调试输出
		static bool debugOnce = true;
		if (GetDebugMode() && debugOnce) {
			std::ofstream debug("debug.txt", std::ios::app);
			debug << "ProcessSDR (BGRA) called." << std::endl;
			debugOnce = false;
		}

		for (int y : std::views::iota(0, height)) {
			auto* srcRow = src + y * pitch;
			auto* dstRow = dst + y * width * 3;

			for (int x : std::views::iota(0, width)) {
				dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // R
				dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
				dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // B
			}
		}
	}

	static float ReinhardToneMapping(float x) {
		return x / (1.0f + x);
	}

	static float ACESFilmToneMapping(float x) {
		const float a = 2.51f;
		const float b = 0.03f;
		const float c = 2.43f;
		const float d = 0.59f;
		const float e = 0.14f;
		return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
	}

	static float LinearToSRGB(float linear) noexcept {
		if (linear <= 0.0031308f) {
			return 12.92f * linear;
		}
		else {
			return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		}
	}

	static void Rec2020ToSRGB(float& r, float& g, float& b) noexcept {
		// 使用Bradford色度适应的精确转换矩阵
		float r2 = 1.7166511f * r - 0.3556708f * g - 0.2533663f * b;
		float g2 = -0.6666844f * r + 1.6164812f * g + 0.0157685f * b;
		float b2 = 0.0176399f * r - 0.0427706f * g + 0.9421031f * b;

		r = std::clamp(r2, 0.0f, 1.0f);
		g = std::clamp(g2, 0.0f, 1.0f);
		b = std::clamp(b2, 0.0f, 1.0f);
	}

	static float PQToLinear(float pq) noexcept {
		// ST2084 EOTF - 更精确的实现
		constexpr float m1 = 2610.0f / 16384.0f;
		constexpr float m2 = 2523.0f / 4096.0f * 128.0f;
		constexpr float c1 = 3424.0f / 4096.0f;
		constexpr float c2 = 2413.0f / 4096.0f * 32.0f;
		constexpr float c3 = 2392.0f / 4096.0f * 32.0f;

		pq = std::clamp(pq, 0.0f, 1.0f);

		if (pq == 0.0f) return 0.0f;

		float p = std::pow(pq, 1.0f / m2);
		float num = std::max(p - c1, 0.0f);
		float den = c2 - c3 * p;

		if (den <= 0.0f) return 0.0f;

		return std::pow(num / den, 1.0f / m1) * 10000.0f;
	}

	static float HalfToFloat(uint16_t h) {
		uint16_t h_exp = (h & 0x7C00) >> 10;
		uint16_t h_sig = h & 0x03FF;
		uint32_t f_sgn = (h & 0x8000) << 16;
		uint32_t f_exp, f_sig;

		if (h_exp == 0) {
			if (h_sig == 0) {
				f_exp = 0;
				f_sig = 0;
			}
			else {
				// Subnormal half-precision number
				h_exp += 1;
				while ((h_sig & 0x0400) == 0) {
					h_sig <<= 1;
					h_exp -= 1;
				}
				h_sig &= 0x03FF;
				f_exp = (h_exp + (127 - 15)) << 23;
				f_sig = h_sig << 13;
			}
		}
		else if (h_exp == 0x1F) {
			f_exp = 0xFF << 23;
			f_sig = h_sig << 13;
		}
		else {
			f_exp = (h_exp + (127 - 15)) << 23;
			f_sig = h_sig << 13;
		}

		uint32_t f = f_sgn | f_exp | f_sig;
		float result;
		memcpy(&result, &f, sizeof(result));
		return result;
	}

	bool SavePNG(std::span<const uint8_t> data, int width, int height, const std::string& filename) {
		Bitmap bitmap(width, height, PixelFormat24bppRGB);
		BitmapData bitmapData;
		Rect rect(0, 0, width, height);

		if (bitmap.LockBits(&rect, ImageLockModeWrite, PixelFormat24bppRGB, &bitmapData) == Ok) {
			auto* dst = static_cast<uint8_t*>(bitmapData.Scan0);
			for (int y : std::views::iota(0, height)) {
				auto* dstRow = dst + y * bitmapData.Stride;
				auto* srcRow = data.data() + y * width * 3;
				for (int x : std::views::iota(0, width)) {
					dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
					dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
					dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
				}
			}
			bitmap.UnlockBits(&bitmapData);

			if (!filename.empty()) {
				CLSID pngClsid;
				GetEncoderClsid(L"image/png", &pngClsid);

				auto wfilename = std::wstring(filename.begin(), filename.end());
				return bitmap.Save(wfilename.c_str(), &pngClsid, nullptr) == Ok;
			}

			return true;
		}
		return false;
	}

	bool SaveToClipboard(std::span<const uint8_t> data, int width, int height) {
		if (!OpenClipboard(nullptr)) return false;

		auto clipboardGuard = [](void*) { CloseClipboard(); };
		std::unique_ptr<void, decltype(clipboardGuard)> guard(reinterpret_cast<void*>(1), clipboardGuard);

		EmptyClipboard();

		// 计算位图数据大小
		int rowSize = ((width * 24 + 31) / 32) * 4; // 4字节对齐
		int imageSize = rowSize * height;
		int totalSize = sizeof(BITMAPINFOHEADER) + imageSize;

		auto hDib = GlobalAlloc(GMEM_MOVEABLE, totalSize);
		if (!hDib) return false;

		auto* bih = static_cast<BITMAPINFOHEADER*>(GlobalLock(hDib));
		if (!bih) {
			GlobalFree(hDib);
			return false;
		}

		// 填充位图信息头
		*bih = BITMAPINFOHEADER{
			.biSize = sizeof(BITMAPINFOHEADER),
			.biWidth = width,
			.biHeight = height, // 正数表示从下到上
			.biPlanes = 1,
			.biBitCount = 24,
			.biCompression = BI_RGB,
			.biSizeImage = static_cast<DWORD>(imageSize)
		};

		// 复制图像数据（需要垂直翻转，RGB转BGR）
		auto* dibData = reinterpret_cast<uint8_t*>(bih + 1);
		for (int y = 0; y < height; ++y) {
			auto* dstRow = dibData + (height - 1 - y) * rowSize;
			auto* srcRow = data.data() + y * width * 3;
			for (int x = 0; x < width; ++x) {
				dstRow[x * 3 + 0] = srcRow[x * 3 + 2]; // B
				dstRow[x * 3 + 1] = srcRow[x * 3 + 1]; // G
				dstRow[x * 3 + 2] = srcRow[x * 3 + 0]; // R
			}
		}

		GlobalUnlock(hDib);

		if (SetClipboardData(CF_DIB, hDib)) {
			return true; // 成功时不释放内存，系统会管理
		}
		else {
			GlobalFree(hDib);
			return false;
		}
	}

	static bool GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
		UINT num = 0, size = 0;
		GetImageEncodersSize(&num, &size);
		if (size == 0) return false;

		auto pImageCodecInfo = std::make_unique<uint8_t[]>(size);
		auto* codecInfo = reinterpret_cast<ImageCodecInfo*>(pImageCodecInfo.get());

		GetImageEncoders(num, size, codecInfo);

		for (UINT j : std::views::iota(0u, num)) {
			if (wcscmp(codecInfo[j].MimeType, format) == 0) {
				*pClsid = codecInfo[j].Clsid;
				return true;
			}
		}

		return false;
	}
};