#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gifler::win32 {

class unique_handle {
public:
    unique_handle() = default;
    explicit unique_handle(HANDLE handle) noexcept : handle_(handle) {}
    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;
    unique_handle(unique_handle&& other) noexcept : handle_(other.release()) {}
    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~unique_handle() { reset(); }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

    HANDLE release() noexcept {
        HANDLE h = handle_;
        handle_ = nullptr;
        return h;
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

class unique_hglobal {
public:
    unique_hglobal() = default;
    explicit unique_hglobal(HGLOBAL handle) noexcept : handle_(handle) {}
    unique_hglobal(const unique_hglobal&) = delete;
    unique_hglobal& operator=(const unique_hglobal&) = delete;
    unique_hglobal(unique_hglobal&& other) noexcept : handle_(other.release()) {}
    unique_hglobal& operator=(unique_hglobal&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~unique_hglobal() { reset(); }

    [[nodiscard]] HGLOBAL get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    HGLOBAL release() noexcept {
        HGLOBAL h = handle_;
        handle_ = nullptr;
        return h;
    }

    void reset(HGLOBAL handle = nullptr) noexcept {
        if (handle_ != nullptr) {
            GlobalFree(handle_);
        }
        handle_ = handle;
    }

private:
    HGLOBAL handle_ = nullptr;
};

[[nodiscard]] std::wstring last_error_message(DWORD error = GetLastError());
[[nodiscard]] std::wstring hresult_message(HRESULT hr);
[[nodiscard]] std::wstring utf8_to_wide(std::string_view value);
[[nodiscard]] std::string wide_to_utf8(std::wstring_view value);
[[nodiscard]] std::wstring quote_arg(std::wstring_view arg);
[[nodiscard]] std::vector<std::wstring> split_path_env();

} // namespace gifler::win32
