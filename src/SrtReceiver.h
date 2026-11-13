#pragma once

#include <srt.h>

#include "Config.h"
#include "TsBuffer.h"
#include "WinHandle.h"

#include <thread>

namespace bondriver {

class SrtReceiver {
  public:
    explicit SrtReceiver(TsBuffer &buffer) : buffer_(buffer) {}
    ~SrtReceiver() { Stop(); }

    bool IsValid() const noexcept { return static_cast<bool>(stopped_); }
    HANDLE StoppedEvent() const noexcept { return stopped_.Get(); }
    bool Start(const Config &config, DWORD channel);
    void Stop() noexcept;

  private:
    void ReceiveLoop() noexcept;
    bool Drain(SRTSOCKET socket);

    TsBuffer &buffer_;
    WinHandle stopped_{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
    SRTSOCKET listener_ = SRT_INVALID_SOCK;
    int poll_ = -1;
    bool runtimeStarted_ = false;
    std::thread worker_;
};

} // namespace bondriver
