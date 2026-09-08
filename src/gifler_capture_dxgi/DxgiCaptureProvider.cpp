#include "gifler_capture_dxgi/DxgiCaptureProvider.h"

#include "gifler_core/Geometry.h"
#include "gifler_win32/Win32Util.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>

namespace gifler::capture_dxgi {
namespace {

using Microsoft::WRL::ComPtr;

struct SelectedOutput {
    ComPtr<IDXGIAdapter1> adapter{};
    ComPtr<IDXGIOutput> output{};
    DXGI_OUTPUT_DESC desc{};
};

bool create_factory(ComPtr<IDXGIFactory1>& factory, std::wstring* error) {
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"CreateDXGIFactory1 failed: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }
    return true;
}

bool select_output_for_rect(gifler::core::PixelRect captureRect, SelectedOutput& selected, std::wstring* error) {
    ComPtr<IDXGIFactory1> factory;
    if (!create_factory(factory, error)) {
        return false;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            continue;
        }

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                continue;
            }

            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) {
                continue;
            }

            gifler::core::PixelRect outputRect{desc.DesktopCoordinates.left, desc.DesktopCoordinates.top,
                                               desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                                               desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top};
            const auto overlap = gifler::core::intersection(captureRect, outputRect);
            if (!overlap.empty() && overlap == captureRect) {
                selected.adapter = adapter;
                selected.output = output;
                selected.desc = desc;
                return true;
            }
        }
    }

    if (error != nullptr) {
        *error = L"No single DXGI output fully contains the requested capture rectangle. Multi-monitor stitching is not implemented in this spike.";
    }
    return false;
}

bool create_d3d_device_for_adapter(IDXGIAdapter1* adapter, ComPtr<ID3D11Device>& device,
                                   ComPtr<ID3D11DeviceContext>& context, std::wstring* error) {
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    D3D_FEATURE_LEVEL createdLevel{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &device, &createdLevel, &context);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"D3D11CreateDevice failed: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }
    return true;
}

struct IconInfoGuard {
    ICONINFO info{};
    bool valid = false;

    ~IconInfoGuard() {
        if (info.hbmColor != nullptr) {
            DeleteObject(info.hbmColor);
        }
        if (info.hbmMask != nullptr) {
            DeleteObject(info.hbmMask);
        }
    }
};

gifler::core::PixelSize cursor_bitmap_size(const ICONINFO& iconInfo) {
    BITMAP bitmap{};
    if (iconInfo.hbmColor != nullptr && GetObjectW(iconInfo.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        return {bitmap.bmWidth, bitmap.bmHeight};
    }
    if (iconInfo.hbmMask != nullptr && GetObjectW(iconInfo.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        return {bitmap.bmWidth, bitmap.bmHeight / 2};
    }
    return {GetSystemMetrics(SM_CXCURSOR), GetSystemMetrics(SM_CYCURSOR)};
}

bool overlay_cursor(gifler::core::PixelRect desktopRect, gifler::core::BgraFrame& frame, std::wstring* error) {
    CURSORINFO cursorInfo{};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (!GetCursorInfo(&cursorInfo)) {
        if (error != nullptr) {
            *error = L"GetCursorInfo failed: " + gifler::win32::hresult_message(HRESULT_FROM_WIN32(GetLastError()));
        }
        return false;
    }
    if ((cursorInfo.flags & CURSOR_SHOWING) == 0 || cursorInfo.hCursor == nullptr) {
        return true;
    }

    IconInfoGuard icon;
    icon.valid = GetIconInfo(cursorInfo.hCursor, &icon.info) == TRUE;
    const gifler::core::PixelPoint hotSpot{icon.valid ? static_cast<int>(icon.info.xHotspot) : 0,
                                           icon.valid ? static_cast<int>(icon.info.yHotspot) : 0};
    const auto cursorSize = icon.valid ? cursor_bitmap_size(icon.info)
                                       : gifler::core::PixelSize{GetSystemMetrics(SM_CXCURSOR), GetSystemMetrics(SM_CYCURSOR)};
    const int cursorLeft = cursorInfo.ptScreenPos.x - hotSpot.x - desktopRect.x;
    const int cursorTop = cursorInfo.ptScreenPos.y - hotSpot.y - desktopRect.y;
    const gifler::core::PixelRect cursorRect{cursorLeft, cursorTop, cursorSize.width, cursorSize.height};
    const gifler::core::PixelRect frameRect{0, 0, frame.width, frame.height};

    gifler::core::CursorShape metadata{};
    metadata.visible = true;
    metadata.position = {cursorInfo.ptScreenPos.x - desktopRect.x, cursorInfo.ptScreenPos.y - desktopRect.y};
    metadata.hotSpot = hotSpot;
    metadata.size = cursorSize;
    frame.cursor = std::move(metadata);

    if (gifler::core::intersection(cursorRect, frameRect).empty()) {
        return true;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = frame.width;
    info.bmiHeader.biHeight = -frame.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* dibBits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (bitmap == nullptr || dibBits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (error != nullptr) {
            *error = L"CreateDIBSection for cursor overlay failed: " + gifler::win32::hresult_message(HRESULT_FROM_WIN32(GetLastError()));
        }
        return false;
    }

    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
        DeleteObject(bitmap);
        if (error != nullptr) {
            *error = L"CreateCompatibleDC for cursor overlay failed: " + gifler::win32::hresult_message(HRESULT_FROM_WIN32(GetLastError()));
        }
        return false;
    }

    std::memcpy(dibBits, frame.pixels.data(), frame.pixels.size());
    HGDIOBJ previous = SelectObject(dc, bitmap);
    DrawIconEx(dc, cursorLeft, cursorTop, cursorInfo.hCursor, cursorSize.width, cursorSize.height, 0, nullptr, DI_NORMAL);
    SelectObject(dc, previous);
    DeleteDC(dc);

    std::memcpy(frame.pixels.data(), dibBits, frame.pixels.size());
    DeleteObject(bitmap);
    return true;
}

} // namespace

struct DxgiCaptureProvider::Impl {
    SelectedOutput selected{};
    ComPtr<ID3D11Device> device{};
    ComPtr<ID3D11DeviceContext> context{};
    ComPtr<IDXGIOutputDuplication> duplication{};
    ComPtr<ID3D11Texture2D> staging{};
    gifler::core::PixelRect outputRect{};
    gifler::core::PixelRect stagingRect{};
    gifler::core::PixelRect cachedRect{};
    gifler::core::BgraFrame cachedFrame{};
    DXGI_FORMAT stagingFormat = DXGI_FORMAT_UNKNOWN;

    ~Impl() {
        reset();
    }

    void reset() {
        cachedFrame = {};
        cachedRect = {};
        staging.Reset();
        duplication.Reset();
        if (context != nullptr) {
            context->ClearState();
            context->Flush();
        }
        context.Reset();
        device.Reset();
        selected = {};
        outputRect = {};
        stagingRect = {};
        stagingFormat = DXGI_FORMAT_UNKNOWN;
    }

    bool contains(gifler::core::PixelRect rect) const {
        return duplication != nullptr && gifler::core::intersection(rect, outputRect) == rect;
    }

    bool initialize(gifler::core::PixelRect rect, std::wstring* error) {
        reset();
        if (!select_output_for_rect(rect, selected, error) ||
            !create_d3d_device_for_adapter(selected.adapter.Get(), device, context, error)) {
            reset();
            return false;
        }

        ComPtr<IDXGIOutput1> output1;
        HRESULT hr = selected.output.As(&output1);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = L"IDXGIOutput1 not available: " + gifler::win32::hresult_message(hr);
            }
            reset();
            return false;
        }

        hr = output1->DuplicateOutput(device.Get(), &duplication);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = L"DuplicateOutput failed: " + gifler::win32::hresult_message(hr);
            }
            reset();
            return false;
        }

        const RECT& coordinates = selected.desc.DesktopCoordinates;
        outputRect = {coordinates.left, coordinates.top, coordinates.right - coordinates.left,
                      coordinates.bottom - coordinates.top};
        return true;
    }

    bool ensure_staging(const D3D11_TEXTURE2D_DESC& sourceDesc, gifler::core::PixelRect rect, std::wstring* error) {
        if (staging != nullptr && stagingRect.width == rect.width && stagingRect.height == rect.height &&
            stagingFormat == sourceDesc.Format) {
            return true;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
        stagingDesc.Width = static_cast<UINT>(rect.width);
        stagingDesc.Height = static_cast<UINT>(rect.height);
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> replacement;
        const HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &replacement);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = L"Create staging texture failed: " + gifler::win32::hresult_message(hr);
            }
            return false;
        }

        staging = std::move(replacement);
        stagingRect = {0, 0, rect.width, rect.height};
        stagingFormat = sourceDesc.Format;
        return true;
    }

    bool capture(gifler::core::PixelRect rect, gifler::core::BgraFrame& output, std::wstring* error) {
        if (!contains(rect) && !initialize(rect, error)) {
            return false;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        ComPtr<IDXGIResource> desktopResource;
        HRESULT hr = duplication->AcquireNextFrame(cachedFrame.empty() ? 100 : 0, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (cachedRect == rect && !cachedFrame.empty()) {
                output = cachedFrame;
                LARGE_INTEGER qpc{};
                QueryPerformanceCounter(&qpc);
                output.timestampTicks = qpc.QuadPart;
                return true;
            }
            if (error != nullptr) {
                *error = L"Timed out waiting for the first desktop frame.";
            }
            return false;
        }
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = L"AcquireNextFrame failed: " + gifler::win32::hresult_message(hr);
            }
            reset();
            return false;
        }

        struct ReleaseFrameGuard {
            IDXGIOutputDuplication* value = nullptr;
            ~ReleaseFrameGuard() {
                if (value != nullptr) {
                    value->ReleaseFrame();
                }
            }
        } releaseFrame{duplication.Get()};

        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = L"Captured desktop resource was not a texture: " + gifler::win32::hresult_message(hr);
            }
            return false;
        }

        D3D11_TEXTURE2D_DESC sourceDesc{};
        desktopTexture->GetDesc(&sourceDesc);
        if (selected.desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && selected.desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
            if (error) *error = L"Rotated displays are not supported by this capture backend.";
            return false;
        }
        if (!ensure_staging(sourceDesc, rect, error)) {
            return false;
        }

        D3D11_BOX sourceBox{};
        sourceBox.left = static_cast<UINT>(rect.x - outputRect.x);
        sourceBox.top = static_cast<UINT>(rect.y - outputRect.y);
        sourceBox.front = 0;
        sourceBox.right = sourceBox.left + static_cast<UINT>(rect.width);
        sourceBox.bottom = sourceBox.top + static_cast<UINT>(rect.height);
        sourceBox.back = 1;
        if (sourceBox.right > sourceDesc.Width || sourceBox.bottom > sourceDesc.Height) {
            if (error) *error = L"Capture rectangle exceeds the desktop texture.";
            return false;
        }
        context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &sourceBox);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            if (error != nullptr) {
                *error = L"Map staging texture failed: " + gifler::win32::hresult_message(hr);
            }
            return false;
        }
        struct UnmapGuard {
            ID3D11DeviceContext* context;
            ID3D11Texture2D* texture;
            ~UnmapGuard() { context->Unmap(texture, 0); }
        } unmap{context.Get(), staging.Get()};

        output.width = rect.width;
        output.height = rect.height;
        output.stride = rect.width * 4;
        output.pixels.resize(static_cast<std::size_t>(output.stride) * static_cast<std::size_t>(output.height));
        const auto* source = static_cast<const unsigned char*>(mapped.pData);
        auto* destination = reinterpret_cast<unsigned char*>(output.pixels.data());
        for (int y = 0; y < output.height; ++y) {
            std::memcpy(destination + static_cast<std::size_t>(y) * static_cast<std::size_t>(output.stride),
                        source + static_cast<std::size_t>(y) * static_cast<std::size_t>(mapped.RowPitch),
                        static_cast<std::size_t>(output.width) * 4u);
        }

        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        output.timestampTicks = qpc.QuadPart;
        output.durationTicks = 0;
        output.changedBounds = {0, 0, output.width, output.height};
        cachedRect = rect;
        cachedFrame = output;
        return true;
    }
};

DxgiCaptureProvider::DxgiCaptureProvider() : impl_(std::make_unique<Impl>()) {}

DxgiCaptureProvider::~DxgiCaptureProvider() = default;

bool DxgiCaptureProvider::is_available(std::wstring* error) const {
    ComPtr<IDXGIFactory1> factory;
    if (!create_factory(factory, error)) {
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    HRESULT hr = factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"No DXGI adapter available: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    return create_d3d_device_for_adapter(adapter.Get(), device, context, error);
}

std::vector<MonitorInfo> DxgiCaptureProvider::enumerate_monitors(std::wstring* error) const {
    std::vector<MonitorInfo> monitors;
    ComPtr<IDXGIFactory1> factory;
    if (!create_factory(factory, error)) {
        return monitors;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            continue;
        }

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                continue;
            }

            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc))) {
                continue;
            }
            MonitorInfo info{};
            info.deviceName = desc.DeviceName;
            info.desktopRect = {desc.DesktopCoordinates.left, desc.DesktopCoordinates.top,
                                desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                                desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top};
            info.attachedToDesktop = desc.AttachedToDesktop == TRUE;
            monitors.push_back(info);
        }
    }
    return monitors;
}

bool DxgiCaptureProvider::capture_one_frame(gifler::core::PixelRect desktopRect, gifler::core::BgraFrame& output,
                                            std::wstring* error, bool captureCursor) const {
    if (desktopRect.empty()) {
        if (error != nullptr) {
            *error = L"Capture rectangle is empty.";
        }
        return false;
    }

    SelectedOutput selected;
    if (!select_output_for_rect(desktopRect, selected, error)) {
        return false;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (!create_d3d_device_for_adapter(selected.adapter.Get(), device, context, error)) {
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    HRESULT hr = selected.output.As(&output1);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"IDXGIOutput1 not available: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }

    ComPtr<IDXGIOutputDuplication> duplication;
    hr = output1->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"DuplicateOutput failed. Close protected/fullscreen surfaces and try again. Details: " +
                     gifler::win32::hresult_message(hr);
        }
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    hr = duplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (error != nullptr) {
            *error = L"Timed out waiting for a desktop frame.";
        }
        return false;
    }
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"AcquireNextFrame failed: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }

    struct ReleaseFrameGuard {
        IDXGIOutputDuplication* duplication = nullptr;
        ~ReleaseFrameGuard() {
            if (duplication != nullptr) {
                duplication->ReleaseFrame();
            }
        }
    } releaseGuard{duplication.Get()};

    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"Captured desktop resource was not a texture: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }

    D3D11_TEXTURE2D_DESC sourceDesc{};
    desktopTexture->GetDesc(&sourceDesc);

    D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
    stagingDesc.Width = static_cast<UINT>(desktopRect.width);
    stagingDesc.Height = static_cast<UINT>(desktopRect.height);
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"Create staging texture failed: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }

    const RECT& out = selected.desc.DesktopCoordinates;
    D3D11_BOX sourceBox{};
    sourceBox.left = static_cast<UINT>(desktopRect.x - out.left);
    sourceBox.top = static_cast<UINT>(desktopRect.y - out.top);
    sourceBox.front = 0;
    sourceBox.right = sourceBox.left + static_cast<UINT>(desktopRect.width);
    sourceBox.bottom = sourceBox.top + static_cast<UINT>(desktopRect.height);
    sourceBox.back = 1;

    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &sourceBox);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"Map staging texture failed: " + gifler::win32::hresult_message(hr);
        }
        return false;
    }

    struct UnmapGuard {
        ID3D11DeviceContext* context = nullptr;
        ID3D11Texture2D* texture = nullptr;
        ~UnmapGuard() {
            if (context != nullptr && texture != nullptr) {
                context->Unmap(texture, 0);
            }
        }
    } unmapGuard{context.Get(), staging.Get()};

    output.width = desktopRect.width;
    output.height = desktopRect.height;
    output.stride = desktopRect.width * 4;
    output.pixels.resize(static_cast<std::size_t>(output.stride) * static_cast<std::size_t>(output.height));

    const auto* src = static_cast<const unsigned char*>(mapped.pData);
    auto* dst = reinterpret_cast<unsigned char*>(output.pixels.data());
    for (int y = 0; y < output.height; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(output.stride),
                    src + static_cast<std::size_t>(y) * static_cast<std::size_t>(mapped.RowPitch),
                    static_cast<std::size_t>(output.width) * 4u);
    }

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    output.timestampTicks = qpc.QuadPart;
    output.durationTicks = 0;
    output.changedBounds = gifler::core::PixelRect{0, 0, output.width, output.height};

    if (captureCursor && !overlay_cursor(desktopRect, output, error)) {
        return false;
    }
    return true;
}

bool DxgiCaptureProvider::capture_composed_frame(gifler::core::PixelRect desktopRect, gifler::core::BgraFrame& output,
                                                 std::wstring* error, bool captureCursor) {
    if (desktopRect.empty()) {
        if (error != nullptr) {
            *error = L"Capture rectangle is empty.";
        }
        return false;
    }

    if (!impl_->capture(desktopRect, output, error)) {
        return false;
    }
    return !captureCursor || overlay_cursor(desktopRect, output, error);
}

} // namespace gifler::capture_dxgi
