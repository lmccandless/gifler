#include "gifler_win32/Process.h"

#include "gifler_win32/Win32Util.h"

namespace gifler::win32 {

std::wstring join_command_line(const std::filesystem::path& exe, const std::vector<std::wstring>& args) {
    std::wstring command = quote_arg(exe.wstring());
    for (const auto& arg : args) {
        command.push_back(L' ');
        command += quote_arg(arg);
    }
    return command;
}

ProcessResult run_process_wait(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                               const std::filesystem::path& workingDirectory) {
    ProcessResult result{};
    std::wstring commandLine = join_command_line(exe, args);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    std::wstring cwd = workingDirectory.empty() ? std::wstring{} : workingDirectory.wstring();
    BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    if (!ok) {
        result.errorMessage = last_error_message();
        return result;
    }

    result.launched = true;
    unique_handle processHandle(process.hProcess);
    unique_handle threadHandle(process.hThread);
    WaitForSingleObject(processHandle.get(), INFINITE);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(processHandle.get(), &exitCode)) {
        result.exitCode = exitCode;
    }
    return result;
}

ProcessResult run_process_with_input(const std::filesystem::path& exe,
                                     const std::vector<std::wstring>& args,
                                     const std::function<bool(HANDLE)>& writeInput,
                                     const std::filesystem::path& workingDirectory) {
    ProcessResult result{};
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE inputReadRaw = nullptr;
    HANDLE inputWriteRaw = nullptr;
    if (!CreatePipe(&inputReadRaw, &inputWriteRaw, &security, 0)) {
        result.errorMessage = last_error_message();
        return result;
    }
    unique_handle inputRead(inputReadRaw);
    unique_handle inputWrite(inputWriteRaw);
    SetHandleInformation(inputWrite.get(), HANDLE_FLAG_INHERIT, 0);

    unique_handle nullOutput(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!nullOutput) {
        result.errorMessage = last_error_message();
        return result;
    }

    std::wstring commandLine = join_command_line(exe, args);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead.get();
    startup.hStdOutput = nullOutput.get();
    startup.hStdError = nullOutput.get();
    PROCESS_INFORMATION process{};

    std::wstring cwd = workingDirectory.empty() ? std::wstring{} : workingDirectory.wstring();
    BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    if (!ok) {
        result.errorMessage = last_error_message();
        return result;
    }

    result.launched = true;
    unique_handle processHandle(process.hProcess);
    unique_handle threadHandle(process.hThread);
    inputRead.reset();

    const bool wroteInput = writeInput(inputWrite.get());
    inputWrite.reset();
    WaitForSingleObject(processHandle.get(), INFINITE);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(processHandle.get(), &exitCode)) {
        result.exitCode = exitCode;
    }
    if (!wroteInput && result.exitCode == 0) {
        result.exitCode = ERROR_WRITE_FAULT;
        result.errorMessage = L"Could not stream recording frames to the encoder.";
    }
    return result;
}

} // namespace gifler::win32
