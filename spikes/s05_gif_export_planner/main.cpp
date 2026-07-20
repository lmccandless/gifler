#include "gifler_export/GifExportPlanner.h"

#include <algorithm>
#include <iostream>

int wmain() {
    gifler::exporting::GifExportRequest request{};
    request.sourceWidth = 640;
    request.sourceHeight = 360;
    request.requestedFps = 15;
    request.outputPath = L"out.gif";
    request.ffmpegPath = L"ffmpeg.exe";
    request.gifskiPath = L"gifski.exe";
    request.gifsiclePath = L"gifsicle.exe";

    gifler::exporting::TargetSizeOptions target{};
    auto plan = gifler::exporting::plan_gif_attempts(request, target);

    std::wcout << L"Target display MB: " << plan.target.displayTargetMb << L"\n";
    std::wcout << L"Target internal MB: " << plan.target.internalSafetyTargetMb << L"\n";
    std::wcout << L"Attempts: " << plan.attempts.size() << L"\n";
    if (!plan.warning.empty()) {
        std::wcout << L"Warning: " << plan.warning << L"\n";
    }

    const std::size_t show = std::min<std::size_t>(plan.attempts.size(), 20);
    for (std::size_t i = 0; i < show; ++i) {
        const auto& c = plan.attempts[i];
        std::wcout << i + 1 << L". " << gifler::exporting::engine_name(c.engine) << L" width=" << c.width
                   << L" fps=" << c.fps << L" quality=" << c.quality << L" colors=" << c.colors
                   << (c.useGifsicle ? L" +gifsicle" : L"") << L"\n";
    }

    std::wcout << L"\nExample commands:\n";
    std::wcout << gifler::exporting::build_ffmpeg_palettegen_command(L"ffmpeg.exe", L"frames\\frame_%06d.png", L"palette.png", 10, 640, 256) << L"\n";
    std::wcout << gifler::exporting::build_ffmpeg_paletteuse_command(L"ffmpeg.exe", L"frames\\frame_%06d.png", L"palette.png", L"out.gif", 10, 640, gifler::exporting::GifDitherMode::Sierra2_4A) << L"\n";
    std::wcout << gifler::exporting::build_gifski_command(L"gifski.exe", L"frames\\frame_*.png", L"out.gif", 10, 640, 90) << L"\n";
    return plan.attempts.empty() ? 1 : 0;
}
