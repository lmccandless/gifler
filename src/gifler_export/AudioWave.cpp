#include "gifler_export/AudioWave.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace gifler::exporting {
void write_audio_wave(const std::filesystem::path& path, const gifler::core::AudioRecording& audio,
                      const std::vector<gifler::core::BgraFrame>& frames) {
    if (!audio.sampleRate || !audio.blockAlign || audio.waveFormat.size() < 16)
        throw std::runtime_error("Audio format is unavailable");
    std::int64_t totalTicks = 0;
    for (const auto& frame : frames) totalTicks += std::max<std::int64_t>(0, frame.durationTicks);
    const auto count = totalTicks * audio.sampleRate / 10'000'000;
    const auto dataSize = static_cast<std::uint64_t>(count) * audio.blockAlign;
    const auto fmtSize = static_cast<std::uint32_t>(audio.waveFormat.size());
    if (dataSize > std::numeric_limits<std::uint32_t>::max() - fmtSize - 64)
        throw std::runtime_error("Audio exceeds WAV file size limit");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    const auto u32 = [&](std::uint32_t value) { stream.write(reinterpret_cast<const char*>(&value), 4); };
    stream.write("RIFF", 4); u32(static_cast<std::uint32_t>(dataSize) + 20 + fmtSize + (fmtSize & 1));
    stream.write("WAVEfmt ", 8); u32(fmtSize);
    stream.write(reinterpret_cast<const char*>(audio.waveFormat.data()), fmtSize);
    if (fmtSize & 1) stream.put(0);
    stream.write("data", 4); u32(static_cast<std::uint32_t>(dataSize));
    std::int64_t elapsedTicks = 0, emitted = 0;
    const char silence[4096]{};
    for (const auto& frame : frames) {
        elapsedTicks += std::max<std::int64_t>(0, frame.durationTicks);
        const auto boundary = elapsedTicks * audio.sampleRate / 10'000'000;
        const auto bytes = static_cast<std::size_t>(boundary - emitted) * audio.blockAlign;
        emitted = boundary;
        const auto offset = static_cast<std::size_t>(std::max<std::int64_t>(0, frame.timestampTicks) *
                            audio.sampleRate / 10'000'000) * audio.blockAlign;
        const auto available = offset < audio.samples.size() ? std::min(bytes, audio.samples.size() - offset) : 0;
        if (available) stream.write(reinterpret_cast<const char*>(audio.samples.data() + offset), available);
        auto remaining = bytes - available;
        while (remaining) {
            const auto chunk = std::min(remaining, sizeof(silence));
            stream.write(silence, chunk);
            remaining -= chunk;
        }
    }
}
}
