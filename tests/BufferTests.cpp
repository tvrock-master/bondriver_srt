#include "TsBuffer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace bondriver;

void Require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<BYTE> Packets(std::size_t count, BYTE value) {
    std::vector<BYTE> data(count * TsPacketSize, value);
    for (std::size_t offset = 0; offset < data.size(); offset += TsPacketSize) {
        data[offset] = 0x47;
    }
    return data;
}

int main() {
    try {
        TsBuffer buffer;
        Require(buffer.IsValid(), "ready event allocation");
        auto packets = Packets(7, 0x12);
        auto malformed = packets;
        malformed[TsPacketSize] = 0;
        Require(buffer.Push(malformed.data(), malformed.size()) == PushResult::InvalidSync,
                "invalid message must be rejected atomically");
        Require(buffer.Push(packets.data(), packets.size() - 1) == PushResult::InvalidLength,
                "partial TS packet must be rejected");
        Require(buffer.Count() == 0, "invalid input must not populate FIFO");

        Require(buffer.Push(packets.data(), packets.size()) == PushResult::Success, "push message");
        Require(WaitForSingleObject(buffer.ReadyEvent(), 0) == WAIT_OBJECT_0, "ready event");
        BYTE *output = nullptr;
        DWORD size = 0;
        DWORD remaining = 0;
        Require(buffer.Read(&output, &size, &remaining), "pointer read");
        Require(size == packets.size() && !remaining &&
                    std::memcmp(output, packets.data(), size) == 0,
                "pointer contents");
        Require(WaitForSingleObject(buffer.ReadyEvent(), 0) == WAIT_TIMEOUT, "empty event reset");
        auto newer = Packets(7, 0x34);
        Require(buffer.Push(newer.data(), newer.size()) == PushResult::Success, "push newer data");
        Require(std::memcmp(output, packets.data(), packets.size()) == 0,
                "receive must not modify a pointer already returned to the host");
        buffer.Clear();
        Require(buffer.Count() == 0 && WaitForSingleObject(buffer.ReadyEvent(), 0) == WAIT_TIMEOUT,
                "purge must reset data and ready event");

        const auto block = Packets(TsBlockSize / TsPacketSize, 0x56);
        for (std::size_t i = 0; i < TsBlockCount; ++i) {
            Require(buffer.Push(block.data(), block.size()) == PushResult::Success, "fill FIFO");
        }
        Require(buffer.Push(packets.data(), packets.size()) == PushResult::Full,
                "overflow must not overwrite queued data");
        std::vector<BYTE> copy(TsBlockSize);
        for (std::size_t i = 0; i < TsBlockCount; ++i) {
            Require(buffer.Read(copy.data(), &size, &remaining), "copy read");
            Require(size == block.size() && copy == block, "FIFO must survive overflow intact");
        }

        // Exercise wraparound and block splitting with input larger than one block.
        const auto crossing = Packets(300, 0x78);
        for (int pass = 0; pass < 100; ++pass) {
            Require(buffer.Push(crossing.data(), crossing.size()) == PushResult::Success,
                    "push across block boundary");
            std::vector<BYTE> received;
            do {
                Require(buffer.Read(&output, &size, &remaining), "wrapped read");
                received.insert(received.end(), output, output + size);
            } while (remaining);
            Require(received == crossing, "wrapped bytes must remain ordered");
        }
        Require(!buffer.Read(static_cast<BYTE *>(nullptr), &size, &remaining), "null destination");
        std::puts("PASS: TS validation, FIFO capacity, ring wrap, purge and pointer lifetime");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
