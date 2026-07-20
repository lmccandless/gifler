#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gifler::exporting {

struct EncoderPaths {
    std::optional<std::filesystem::path> ffmpeg{};
    std::optional<std::filesystem::path> gifski{};
    std::optional<std::filesystem::path> gifsicle{};
};

[[nodiscard]] std::optional<std::filesystem::path> find_encoder(std::wstring executableName,
                                                                std::optional<std::filesystem::path> configuredPath,
                                                                const std::filesystem::path& appDirectory);

[[nodiscard]] EncoderPaths discover_encoders(const EncoderPaths& configured, const std::filesystem::path& appDirectory);

} // namespace gifler::exporting
