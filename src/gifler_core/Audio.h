#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace gifler::core {
struct AudioRecording {
    std::vector<std::byte> waveFormat;
    std::vector<std::byte> samples;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
};
}
