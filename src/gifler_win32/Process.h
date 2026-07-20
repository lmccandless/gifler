#pragma once

#include <Windows.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gifler::win32 {

struct ProcessResult {
    DWORD exitCode = 0;
    bool launched = false;
    std::wstring errorMessage{};
};

[[nodiscard]] std::wstring join_command_line(const std::filesystem::path& exe, const std::vector<std::wstring>& args);
[[nodiscard]] ProcessResult run_process_wait(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                                             const std::filesystem::path& workingDirectory = {});
[[nodiscard]] ProcessResult run_process_with_input(const std::filesystem::path& exe,
                                                   const std::vector<std::wstring>& args,
                                                   const std::function<bool(HANDLE)>& writeInput,
                                                   const std::filesystem::path& workingDirectory = {});

} // namespace gifler::win32
