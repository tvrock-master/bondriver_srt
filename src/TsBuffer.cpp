#include "TsBuffer.h"

#include <algorithm>
#include <cstring>

namespace bondriver {

PushResult TsBuffer::Push(const BYTE *data, std::size_t size) {
    if (!data || !size || size % TsPacketSize) {
        return PushResult::InvalidLength;
    }
    for (std::size_t offset = 0; offset < size; offset += TsPacketSize) {
        if (data[offset] != 0x47) {
            return PushResult::InvalidSync;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t tail = (head_ + count_ + TsBlockCount - 1) % TsBlockCount;
    const std::size_t tailSpace = count_ ? TsBlockSize - (*blocks_)[tail].size : 0;
    if (size > (TsBlockCount - count_) * TsBlockSize + tailSpace) {
        return PushResult::Full;
    }

    while (size) {
        if (!count_ || (*blocks_)[tail].size == TsBlockSize) {
            tail = (head_ + count_) % TsBlockCount;
            (*blocks_)[tail].size = 0;
            ++count_;
        }
        auto &block = (*blocks_)[tail];
        const std::size_t take = std::min(size, TsBlockSize - block.size);
        std::memcpy(block.data.data() + block.size, data, take);
        block.size += take;
        data += take;
        size -= take;
    }
    SetEvent(ready_.Get());
    return PushResult::Success;
}

DWORD TsBuffer::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<DWORD>(count_);
}

void TsBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = count_ = 0;
    ResetEvent(ready_.Get());
}

void TsBuffer::Pop(BYTE *destination, DWORD &size, DWORD &remaining) {
    size = 0;
    if (count_) {
        const auto &block = (*blocks_)[head_];
        size = static_cast<DWORD>(block.size);
        std::memcpy(destination, block.data.data(), block.size);
        head_ = (head_ + 1) % TsBlockCount;
        --count_;
    }
    remaining = static_cast<DWORD>(count_);
    if (!count_) {
        ResetEvent(ready_.Get());
    }
}

bool TsBuffer::Read(BYTE *destination, DWORD *size, DWORD *remaining) {
    if (!destination || !size || !remaining) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Pop(destination, *size, *remaining);
    return true;
}

bool TsBuffer::Read(BYTE **destination, DWORD *size, DWORD *remaining) {
    if (!destination || !size || !remaining) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    *destination = output_.data();
    Pop(output_.data(), *size, *remaining);
    return true;
}

} // namespace bondriver
