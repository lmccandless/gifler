#include "gifler_export/EncoderDiscovery.h"

#include "gifler_win32/Win32Util.h"

#include <Windows.h>

namespace gifler::exporting {

std::optional<std::filesystem::path> find_encoder(std::wstring executableName,
                                                  std::optional<std::filesystem::path> configuredPath,
                                                  const std::filesystem::path& appDirectory) {
    if (configuredPath && std::filesystem::exists(*configuredPath)) {
        return std::filesystem::absolute(*configuredPath);
    }

    const auto local = appDirectory / L"encoders" / executableName;
    if (std::filesystem::exists(local)) {
        return std::filesystem::absolute(local);
    }

    wchar_t buffer[MAX_PATH]{};
    DWORD found = SearchPathW(nullptr, executableName.c_str(), nullptr, MAX_PATH, buffer, nullptr);
    if (found > 0 && found < MAX_PATH) {
        return std::filesystem::path(buffer);
    }

    return std::nullopt;
}

EncoderPaths discover_encoders(const EncoderPaths& configured, const std::filesystem::path& appDirectory) {
    EncoderPaths paths{};
    paths.ffmpeg = find_encoder(L"ffmpeg.exe", configured.ffmpeg, appDirectory);
    paths.gifski = find_encoder(L"gifski.exe", configured.gifski, appDirectory);
    paths.gifsicle = find_encoder(L"gifsicle.exe", configured.gifsicle, appDirectory);
    return paths;
}

} // namespace gifler::exporting
