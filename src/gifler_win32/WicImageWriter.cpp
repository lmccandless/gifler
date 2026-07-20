#include "gifler_win32/WicImageWriter.h"

#include "gifler_win32/Win32Util.h"

#include <wincodec.h>
#include <wrl/client.h>

namespace gifler::win32 {

bool save_bgra_frame_as_png(const gifler::core::BgraFrame& frame, const std::filesystem::path& outputPath,
                            std::wstring* error) {
    if (frame.empty()) {
        if (error != nullptr) {
            *error = L"Cannot write an empty BGRA frame.";
        }
        return false;
    }

    HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninit = SUCCEEDED(coInit);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) {
        if (error != nullptr) {
            *error = hresult_message(coInit);
        }
        return false;
    }

    struct CoInitializeGuard {
        bool shouldUninitialize = false;
        ~CoInitializeGuard() {
            if (shouldUninitialize) {
                CoUninitialize();
            }
        }
    } coInitializeGuard{shouldUninit};

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = hresult_message(hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(outputPath.wstring().c_str(), GENERIC_WRITE);
    }
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"Could not open PNG output: " + hresult_message(hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"Could not initialize PNG encoder: " + hresult_message(hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEncode;
    Microsoft::WRL::ComPtr<IPropertyBag2> propertyBag;
    hr = encoder->CreateNewFrame(&frameEncode, &propertyBag);
    if (SUCCEEDED(hr)) {
        hr = frameEncode->Initialize(propertyBag.Get());
    }
    if (SUCCEEDED(hr)) {
        hr = frameEncode->SetSize(static_cast<UINT>(frame.width), static_cast<UINT>(frame.height));
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = frameEncode->SetPixelFormat(&format);
    }
    if (SUCCEEDED(hr)) {
        hr = frameEncode->WritePixels(static_cast<UINT>(frame.height), static_cast<UINT>(frame.stride),
                                      static_cast<UINT>(frame.pixels.size()),
                                      reinterpret_cast<BYTE*>(const_cast<std::byte*>(frame.pixels.data())));
    }
    if (SUCCEEDED(hr)) {
        hr = frameEncode->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }

    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = L"Could not write PNG: " + hresult_message(hr);
        }
        return false;
    }
    return true;
}

} // namespace gifler::win32
