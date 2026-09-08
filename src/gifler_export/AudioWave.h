#pragma once
#include "gifler_core/Audio.h"
#include "gifler_core/Frame.h"
#include <filesystem>
#include <vector>
namespace gifler::exporting {
void write_audio_wave(const std::filesystem::path& path, const gifler::core::AudioRecording& audio,
                      const std::vector<gifler::core::BgraFrame>& frames);
}
