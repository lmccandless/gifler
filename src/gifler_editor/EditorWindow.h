#pragma once

#include "gifler_core/Frame.h"

#include <Windows.h>

#include <functional>
#include <vector>

namespace gifler::editor {

class EditorWindow {
public:
    using ApplyCallback = std::function<void(std::vector<gifler::core::BgraFrame>)>;

    static void show_placeholder(HWND owner);
    static void show(HWND owner, const std::vector<gifler::core::BgraFrame>& frames, ApplyCallback onApply = {});
};

} // namespace gifler::editor
