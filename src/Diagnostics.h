#pragma once

#include <srt.h>

namespace bondriver {

inline void Report(const char *message) noexcept {
    OutputDebugStringA("BonDriver_SRT: ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

inline void ReportSocketError(const char *operation) noexcept {
    Report(operation);
    OutputDebugStringA(srt_getlasterror_str());
    OutputDebugStringA("\n");
}

template <class T> bool SetOption(SRTSOCKET socket, SRT_SOCKOPT option, const T &value) noexcept {
    return srt_setsockflag(socket, option, &value, sizeof(value)) != SRT_ERROR;
}

} // namespace bondriver
