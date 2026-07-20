#pragma once

#include "gifler_core/Frame.h"

#include <filesystem>
#include <string>

namespace gifler::win32 {

bool save_bgra_frame_as_png(const gifler::core::BgraFrame& frame, const std::filesystem::path& outputPath,
                            std::wstring* error = nullptr);

} // namespace gifler::win32
