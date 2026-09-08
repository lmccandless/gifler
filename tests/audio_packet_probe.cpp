#include "gifler_record/LoopbackAudio.h"
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 3;
    struct ComGuard { ~ComGuard() { CoUninitialize(); } } guard;
    using Microsoft::WRL::ComPtr;
    const auto require = [](HRESULT hr) { if (FAILED(hr)) throw hr; };
    try {
        ComPtr<IMMDeviceEnumerator> enumerator;
        require(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
        ComPtr<IMMDevice> endpoint;
        require(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint));
        ComPtr<IAudioClient> client;
        require(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(client.GetAddressOf())));
        WAVEFORMATEX* format = nullptr;
        require(client->GetMixFormat(&format));
        struct FormatGuard { WAVEFORMATEX* p; ~FormatGuard() { CoTaskMemFree(p); } } formatGuard{format};
        std::cout << "Mix rate=" << format->nSamplesPerSec << " channels=" << format->nChannels
                  << " blockAlign=" << format->nBlockAlign << " bits=" << format->wBitsPerSample << "\n";
        require(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1'000'000, 0, format, nullptr));
        ComPtr<IAudioCaptureClient> capture;
        require(client->GetService(IID_PPV_ARGS(&capture)));
        struct Packet { UINT32 frames; DWORD flags; UINT64 position; UINT64 qpc; std::int64_t arrival; };
        std::vector<Packet> packets;
        packets.reserve(2000);
        require(client->Start());
        struct StopGuard { IAudioClient* p; ~StopGuard() { p->Stop(); } } stopGuard{client.Get()};
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < end) {
            UINT32 size = 0;
            require(capture->GetNextPacketSize(&size));
            while (size) {
                BYTE* data = nullptr;
                Packet packet{};
                require(capture->GetBuffer(&data, &packet.frames, &packet.flags, &packet.position, &packet.qpc));
                packet.arrival = gifler::record::clock_100ns();
                require(capture->ReleaseBuffer(packet.frames));
                packets.push_back(packet);
                require(capture->GetNextPacketSize(&size));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::ofstream file(argv[1]);
        file << "frames,flags,position,qpc100ns,arrival100ns,qpcGapFrames,deviceGapFrames\n";
        Packet previous{};
        for (const auto& packet : packets) {
            const auto qpcGap = previous.frames ?
                (static_cast<std::int64_t>(packet.qpc) - static_cast<std::int64_t>(previous.qpc)) * format->nSamplesPerSec / 10'000'000 - previous.frames : 0;
            const auto deviceGap = previous.frames ? static_cast<std::int64_t>(packet.position - previous.position) - previous.frames : 0;
            file << packet.frames << ',' << packet.flags << ',' << packet.position << ',' << packet.qpc << ',' << packet.arrival << ',' << qpcGap << ',' << deviceGap << '\n';
            previous = packet;
        }
        std::cout << "Packets=" << packets.size() << "; no audio samples retained.\n";
        return packets.empty() ? 4 : 0;
    } catch (HRESULT hr) {
        std::cerr << "Audio probe failed: " << std::hex << hr << "\n";
        return 1;
    }
}
