#include "gifler_win32/Process.h"

#include "gifler_win32/Win32Util.h"
#include <thread>

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
                               const std::filesystem::path& workingDirectory, const std::function<bool()>& canceled) {
    return run_process_with_input(exe, args, [](HANDLE) { return true; }, workingDirectory, canceled);
}

ProcessResult run_process_with_input(const std::filesystem::path& exe,
                                     const std::vector<std::wstring>& args,
                                     const std::function<bool(HANDLE)>& writeInput,
                                     const std::filesystem::path& workingDirectory, const std::function<bool()>& canceled) {
    ProcessResult result{};
    if (canceled && canceled()) {
        result.exitCode = ERROR_CANCELLED;
        result.errorMessage = L"Export canceled.";
        return result;
    }
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

    wchar_t tempDir[MAX_PATH]{}, logPath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDir) || !GetTempFileNameW(tempDir, L"gfl", 0, logPath)) {
        result.errorMessage = last_error_message();
        return result;
    }
    unique_handle errorLog(CreateFileW(logPath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &security, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!errorLog) {
        DeleteFileW(logPath);
        result.errorMessage = last_error_message();
        return result;
    }

    std::wstring commandLine = join_command_line(exe, args);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead.get();
    startup.hStdOutput = nullOutput.get();
    startup.hStdError = errorLog.get();
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

    std::jthread cancellation;
    if (canceled) {
        try {
            cancellation = std::jthread([&](std::stop_token stop) {
                while (!stop.stop_requested() && WaitForSingleObject(processHandle.get(), 50) == WAIT_TIMEOUT) {
                    if (canceled()) {
                        // Only terminate the encoder child created by this operation.
                        TerminateProcess(processHandle.get(), ERROR_CANCELLED);
                        return;
                    }
                }
            });
        } catch (...) {
            TerminateProcess(processHandle.get(), ERROR_NOT_ENOUGH_MEMORY);
            WaitForSingleObject(processHandle.get(), INFINITE);
            throw;
        }
    }

    bool wroteInput = false;
    try {
        wroteInput = writeInput(inputWrite.get());
    } catch (...) {
        // Always deliver EOF and reap the child, even if a frame allocation fails.
        inputWrite.reset();
        WaitForSingleObject(processHandle.get(), INFINITE);
        throw;
    }
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
    LARGE_INTEGER size{};
    if (GetFileSizeEx(errorLog.get(), &size) && size.QuadPart > 0) {
        LARGE_INTEGER offset{};
        offset.QuadPart = size.QuadPart > 4096 ? size.QuadPart - 4096 : 0;
        SetFilePointerEx(errorLog.get(), offset, nullptr, FILE_BEGIN);
        char buffer[4096]{};
        DWORD read = 0;
        if (ReadFile(errorLog.get(), buffer, sizeof(buffer), &read, nullptr)) {
            result.errorMessage.append(buffer, buffer + read);
        }
    }
    return result;
}

} // namespace gifler::win32
