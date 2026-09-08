#include "gifler_core/Settings.h"
#include "gifler_core/AspectRatio.h"

#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>

namespace gifler::core {
namespace {

std::string bool_text(bool value) {
    return value ? "true" : "false";
}

} // namespace

std::filesystem::path default_settings_path() {
    wchar_t* appData = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&appData, &len, L"APPDATA") == 0 && appData != nullptr) {
        std::filesystem::path path(appData);
        std::free(appData);
        return path / L"Gifler" / L"settings.ini";
    }
    if (appData != nullptr) {
        std::free(appData);
    }
    return std::filesystem::current_path() / L"Gifler.settings.ini";
}

AppSettings load_settings_or_defaults(const std::filesystem::path& path) {
    AppSettings settings{};
    std::ifstream input(path);
    if (!input) {
        return settings;
    }

    std::string line;
    while (std::getline(input, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        try {
            if (key == "defaultFps") {
                settings.defaultFps = std::stoi(value);
            } else if (key == "lastExportFormat") {
                settings.lastExportFormat = std::stoi(value);
            } else if (key == "captureCursor") {
                settings.captureCursor = value == "true" || value == "1";
            } else if (key == "captureAudio") {
                settings.captureAudio = value == "true" || value == "1";
            } else if (key == "captureAspectRatio") {
                settings.captureAspectRatio = std::stoi(value);
            } else if (key == "socialMp4") {
                settings.socialMp4 = value == "true" || value == "1";
            } else if (key == "mp4AudioSampleRate") {
                settings.mp4AudioSampleRate = std::stoi(value);
            } else if (key == "target.displayMb") {
                settings.target.displayTargetMb = std::stod(value);
            } else if (key == "target.internalMb") {
                settings.target.internalSafetyTargetMb = std::stod(value);
            } else if (key == "target.minimumFps") {
                settings.target.minimumFps = std::stoi(value);
            } else if (key == "target.minimumWidth") {
                settings.target.minimumWidth = std::stoi(value);
            }
        } catch (...) {
            // Keep defaults for malformed values.
        }
    }
    if (settings.defaultFps < 1 || settings.defaultFps > 240) {
        settings.defaultFps = 10;
    }
    if (settings.lastExportFormat < 0 || settings.lastExportFormat > 3) {
        settings.lastExportFormat = 0;
    }
    if (settings.captureAspectRatio < 0 || settings.captureAspectRatio >= static_cast<int>(CaptureAspectRatios.size()))
        settings.captureAspectRatio = 0;
    if (settings.mp4AudioSampleRate != 44100 && settings.mp4AudioSampleRate != 48000)
        settings.mp4AudioSampleRate = 48000;
    if (!std::isfinite(settings.target.displayTargetMb) || settings.target.displayTargetMb < 0 ||
        settings.target.displayTargetMb > 100) settings.target.displayTargetMb = 10;
    settings.target.internalSafetyTargetMb = settings.target.displayTargetMb * 0.95;
    return settings;
}

void save_settings_best_effort(const std::filesystem::path& path, const AppSettings& settings) {
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::trunc);
        if (!output) {
            return;
        }
        output << "defaultFps=" << settings.defaultFps << "\n";
        output << "lastExportFormat=" << settings.lastExportFormat << "\n";
        output << "captureCursor=" << bool_text(settings.captureCursor) << "\n";
        output << "captureAudio=" << bool_text(settings.captureAudio) << "\n";
        output << "captureAspectRatio=" << settings.captureAspectRatio << "\n";
        output << "socialMp4=" << bool_text(settings.socialMp4) << "\n";
        output << "mp4AudioSampleRate=" << settings.mp4AudioSampleRate << "\n";
        output << "target.displayMb=" << settings.target.displayTargetMb << "\n";
        output << "target.internalMb=" << settings.target.internalSafetyTargetMb << "\n";
        output << "target.minimumFps=" << settings.target.minimumFps << "\n";
        output << "target.minimumWidth=" << settings.target.minimumWidth << "\n";
    } catch (...) {
        // Settings persistence should never prevent app shutdown.
    }
}

} // namespace gifler::core
