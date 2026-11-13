#pragma once

#include <windows.h>

namespace bondriver {

class WinHandle {
  public:
    explicit WinHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~WinHandle() { Reset(); }

    WinHandle(const WinHandle &) = delete;
    WinHandle &operator=(const WinHandle &) = delete;

    HANDLE Get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }

    void Reset(HANDLE handle = nullptr) noexcept {
        if (handle_) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_;
};

} // namespace bondriver
