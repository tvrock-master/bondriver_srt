#pragma once

#include "WinHandle.h"

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>

namespace bondriver {

constexpr std::size_t TsPacketSize = 188;
constexpr std::size_t TsBlockSize = TsPacketSize * 256;
constexpr std::size_t TsBlockCount = 88;

enum class PushResult { Success, InvalidLength, InvalidSync, Full };

// Fixed allocation: the receive thread never grows a container or overwrites
// the buffer returned to the host. All public operations serialize internally.
class TsBuffer {
  public:
    bool IsValid() const noexcept { return static_cast<bool>(ready_); }
    HANDLE ReadyEvent() const noexcept { return ready_.Get(); }
    PushResult Push(const BYTE *data, std::size_t size);
    DWORD Count() const;
    void Clear();
    bool Read(BYTE *destination, DWORD *size, DWORD *remaining);
    bool Read(BYTE **destination, DWORD *size, DWORD *remaining);

  private:
    struct Block {
        std::array<BYTE, TsBlockSize> data;
        std::size_t size = 0;
    };

    void Pop(BYTE *destination, DWORD &size, DWORD &remaining);

    std::unique_ptr<std::array<Block, TsBlockCount>> blocks_{new std::array<Block, TsBlockCount>};
    std::array<BYTE, TsBlockSize> output_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    mutable std::mutex mutex_;
    WinHandle ready_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
};

} // namespace bondriver
