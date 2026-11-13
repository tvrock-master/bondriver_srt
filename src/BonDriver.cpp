#include "SrtReceiver.h"

#include "Diagnostics.h"
#include "IBonDriver2.h"

#include <ws2tcpip.h>

#include <atomic>
#include <cwchar>
#include <mutex>

namespace {

using namespace bondriver;

HMODULE module;

std::wstring OwnershipName(const Config &config, DWORD channel) {
    IN_ADDR address{};
    if (InetPtonW(AF_INET, config.listenAddress.c_str(), &address) != 1) {
        return {};
    }
    wchar_t name[96]{};
    swprintf_s(name, L"Local\\BonDriver_SRT_Endpoint_%08lX_%u",
               static_cast<unsigned long>(ntohl(address.s_addr)),
               static_cast<unsigned>(config.basePort + channel));
    return name;
}

class BonDriverSrt final : public IBonDriver2 {
  public:
    ~BonDriverSrt() { CloseTuner(); }

    const BOOL OpenTuner() override {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        open_ = buffer_.IsValid() && receiver_.IsValid();
        return open_;
    }

    void CloseTuner() override {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        Stop();
        open_ = false;
    }

    const BOOL SetChannel(BYTE channel) override {
        return channel >= 13 && channel < 23 ? SetChannel(0, channel - 13) : FALSE;
    }

    const BOOL SetChannel(DWORD space, DWORD channel) override {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        if (!open_ || space != 0 || channel >= ChannelCount) {
            return FALSE;
        }
        Stop();
        const auto path = ConfigurationPath(module);
        Config config;
        std::string error;
        if (path.empty() || !ReadConfig(path, config, error)) {
            Report(error.empty() ? "Cannot determine the INI path." : error.c_str());
            return FALSE;
        }
        const auto ownershipName = OwnershipName(config, channel);
        if (ownershipName.empty()) {
            Report("ListenAddress must be a numeric IPv4 address.");
            return FALSE;
        }
        ownership_.Reset(CreateMutexW(nullptr, FALSE, ownershipName.c_str()));
        const DWORD status = GetLastError();
        if (!ownership_ || status == ERROR_ALREADY_EXISTS) {
            ownership_.Reset();
            Report("Another BonDriver_SRT instance owns this listening address and port.");
            return FALSE;
        }
        if (!receiver_.Start(config, channel)) {
            Stop();
            return FALSE;
        }
        channel_.store(channel);
        return TRUE;
    }

    const float GetSignalLevel() override { return 0.0f; }

    const DWORD WaitTsStream(DWORD timeout) override {
        const HANDLE events[]{receiver_.StoppedEvent(), buffer_.ReadyEvent()};
        const DWORD result = WaitForMultipleObjects(2, events, FALSE, timeout ? timeout : INFINITE);
        if (result == WAIT_OBJECT_0) {
            return WAIT_ABANDONED;
        }
        return result == WAIT_OBJECT_0 + 1 ? WAIT_OBJECT_0 : result;
    }

    const DWORD GetReadyCount() override { return buffer_.Count(); }

    const BOOL GetTsStream(BYTE *destination, DWORD *size, DWORD *remaining) override {
        return buffer_.Read(destination, size, remaining);
    }

    const BOOL GetTsStream(BYTE **destination, DWORD *size, DWORD *remaining) override {
        return buffer_.Read(destination, size, remaining);
    }

    void PurgeTsStream() override { buffer_.Clear(); }
    LPCTSTR GetTunerName() override { return TEXT("SRT"); }

    const BOOL IsTunerOpening() override {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        return static_cast<bool>(ownership_);
    }

    LPCTSTR EnumTuningSpace(DWORD space) override { return space == 0 ? TEXT("SRT") : nullptr; }

    LPCTSTR EnumChannelName(DWORD space, DWORD channel) override {
        static const TCHAR *const names[]{
            TEXT("SRT 0"), TEXT("SRT 1"), TEXT("SRT 2"), TEXT("SRT 3"), TEXT("SRT 4"),
            TEXT("SRT 5"), TEXT("SRT 6"), TEXT("SRT 7"), TEXT("SRT 8"), TEXT("SRT 9")};
        return space == 0 && channel < ChannelCount ? names[channel] : nullptr;
    }

    const DWORD GetCurSpace() override { return 0; }
    const DWORD GetCurChannel() override { return channel_.load(); }
    void Release() override { delete this; }

  private:
    void Stop() {
        receiver_.Stop();
        buffer_.Clear();
        channel_.store(InvalidChannel);
        ownership_.Reset();
    }

    std::mutex lifecycleMutex_;
    TsBuffer buffer_;
    SrtReceiver receiver_{buffer_};
    WinHandle ownership_;
    std::atomic<DWORD> channel_{InvalidChannel};
    bool open_ = false;
};

} // namespace

extern "C" BONDRIVER_API IBonDriver *CreateBonDriver() {
    try {
        // Every host owns and releases its receiver, FIFO and worker independently.
        return new BonDriverSrt;
    } catch (...) {
        return nullptr;
    }
}

BOOL WINAPI DllMain(HINSTANCE handle, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = handle;
    }
    // Release() must run before FreeLibrary(): shutdown cannot wait for worker
    // threads while Windows holds the loader lock.
    return TRUE;
}
