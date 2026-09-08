#include "gifler_record/LoopbackAudio.h"
#include "gifler_record/AudioPacketTimeline.h"
#include "gifler_win32/Win32Util.h"
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace gifler::record {
std::int64_t clock_100ns() {
    LARGE_INTEGER ticks{}, frequency{};
    QueryPerformanceCounter(&ticks);
    QueryPerformanceFrequency(&frequency);
    return ticks.QuadPart / frequency.QuadPart * 10'000'000 +
           ticks.QuadPart % frequency.QuadPart * 10'000'000 / frequency.QuadPart;
}

void LoopbackAudio::start(std::int64_t epoch100ns) {
    stop();
    recording_ = {};
    error_.clear();
    stopping_ = false;
    worker_ = std::thread([this, epoch100ns] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com)) { error_ = L"Audio COM initialization failed."; return; }
        struct ComGuard { ~ComGuard() { CoUninitialize(); } } comGuard;
        try {
            using Microsoft::WRL::ComPtr;
            auto require = [](HRESULT hr) {
                if (FAILED(hr)) throw hr;
            };
            ComPtr<IMMDeviceEnumerator> enumerator;
            require(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
            ComPtr<IMMDevice> endpoint;
            require(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint));
            ComPtr<IAudioClient> client;
            require(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(client.GetAddressOf())));
            WAVEFORMATEX* format = nullptr;
            require(client->GetMixFormat(&format));
            struct FormatGuard { WAVEFORMATEX* value; ~FormatGuard() { CoTaskMemFree(value); } } formatGuard{format};
            recording_.sampleRate = format->nSamplesPerSec;
            recording_.blockAlign = format->nBlockAlign;
            const auto* bytes = reinterpret_cast<const std::byte*>(format);
            recording_.waveFormat.assign(bytes, bytes + sizeof(WAVEFORMATEX) + format->cbSize);
            require(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                       1'000'000, 0, format, nullptr));
            ComPtr<IAudioCaptureClient> capture;
            require(client->GetService(IID_PPV_ARGS(&capture)));
            UINT32 bufferFrames = 0;
            require(client->GetBufferSize(&bufferFrames));
            std::vector<std::byte> scratch(static_cast<std::size_t>(bufferFrames) * format->nBlockAlign);
            AudioPacketTimeline timeline;
            require(client->Start());
            struct StopGuard { IAudioClient* client; ~StopGuard() { client->Stop(); } } stopGuard{client.Get()};
            while (!stopping_) {
                UINT32 packet = 0;
                require(capture->GetNextPacketSize(&packet));
                while (packet != 0) {
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    UINT64 position = 0, qpc = 0;
                    require(capture->GetBuffer(&data, &frames, &flags, &position, &qpc));
                    struct BufferGuard {
                        IAudioCaptureClient* capture; UINT32 frames;
                        ~BufferGuard() { if (frames) capture->ReleaseBuffer(frames); }
                    } bufferGuard{capture.Get(), frames};
                    auto timestamp = static_cast<std::int64_t>(qpc);
                    if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                        timestamp = clock_100ns() - static_cast<std::int64_t>(frames) * 10'000'000 / format->nSamplesPerSec;
                    const auto clockSample = (timestamp - epoch100ns) * format->nSamplesPerSec / 10'000'000;
                    const auto sample = timeline.place(position, frames, clockSample,
                        !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR));
                    const auto count = static_cast<std::size_t>(frames) * format->nBlockAlign;
                    if (count > scratch.size()) throw std::runtime_error("Invalid audio packet size");
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data)
                        std::memcpy(scratch.data(), data, count);
                    else
                        std::fill_n(scratch.data(), count, std::byte{});
                    // Return the engine buffer before allocating or growing recording storage.
                    require(capture->ReleaseBuffer(frames));
                    bufferGuard.frames = 0;
                    const auto skip = static_cast<std::size_t>(std::min<std::int64_t>(frames, std::max<std::int64_t>(0, -sample))) * format->nBlockAlign;
                    const auto offset = static_cast<std::uint64_t>(std::max<std::int64_t>(0, sample)) * format->nBlockAlign;
                    constexpr std::size_t limit = 512u * 1024u * 1024u;
                    if (offset > limit || count - skip > limit - offset)
                        throw std::runtime_error("Audio recording limit reached");
                    const auto end = static_cast<std::size_t>(offset) + count - skip;
                    if (end > recording_.samples.capacity()) {
                        constexpr std::size_t chunk = 1024u * 1024u;
                        recording_.samples.reserve(std::min(limit, (end + chunk - 1) / chunk * chunk));
                    }
                    recording_.samples.resize(std::max(recording_.samples.size(), end), std::byte{});
                    if (count > skip)
                        std::memcpy(recording_.samples.data() + offset, scratch.data() + skip, count - skip);
                    require(capture->GetNextPacketSize(&packet));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        } catch (HRESULT hr) {
            error_ = L"System audio capture failed: " + gifler::win32::hresult_message(hr);
        } catch (...) {
            error_ = L"System audio capture stopped because its buffer could not be stored.";
        }
    });
}

void LoopbackAudio::stop() {
    stopping_ = true;
    if (worker_.joinable()) worker_.join();
}
}
