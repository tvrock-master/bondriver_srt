#include "SrtReceiver.h"

#include "Diagnostics.h"

#include <array>
#include <mutex>

namespace bondriver {
namespace {

const int ReadEvents = SRT_EPOLL_IN | SRT_EPOLL_ERR;

// srt_startup() is idempotent while running, not a reference-counted lease.
// Keep the runtime alive until the last receiver in this DLL has stopped.
std::mutex runtimeMutex;
size_t runtimeUsers = 0;

bool AcquireRuntime() {
    std::lock_guard<std::mutex> lock(runtimeMutex);
    if (!runtimeUsers && srt_startup() == SRT_ERROR) {
        return false;
    }
    ++runtimeUsers;
    return true;
}

void ReleaseRuntime() {
    std::lock_guard<std::mutex> lock(runtimeMutex);
    if (runtimeUsers && --runtimeUsers == 0) {
        srt_cleanup();
    }
}

class Socket {
  public:
    explicit Socket(SRTSOCKET socket = SRT_INVALID_SOCK) noexcept : socket_(socket) {}
    ~Socket() { Reset(); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    SRTSOCKET Get() const noexcept { return socket_; }
    bool IsValid() const noexcept { return socket_ != SRT_INVALID_SOCK; }
    void Reset(SRTSOCKET socket = SRT_INVALID_SOCK) noexcept {
        if (IsValid()) {
            srt_close(socket_);
        }
        socket_ = socket;
    }

  private:
    SRTSOCKET socket_;
};

} // namespace

bool SrtReceiver::Start(const Config &config, DWORD channel) {
    if (channel >= ChannelCount || !IsValid()) {
        return false;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<u_short>(config.basePort + channel));
    if (InetPtonW(AF_INET, config.listenAddress.c_str(), &local.sin_addr) != 1) {
        Report("ListenAddress must be a numeric IPv4 address.");
        return false;
    }
    if (!AcquireRuntime()) {
        ReportSocketError("Cannot initialize the SRT runtime.");
        return false;
    }
    runtimeStarted_ = true;
    listener_ = srt_create_socket();
    if (listener_ == SRT_INVALID_SOCK) {
        ReportSocketError("Cannot create the listener socket.");
        return false;
    }

    const SRT_TRANSTYPE live = SRTT_LIVE;
    const bool synchronous = false;
    const bool enforceEncryption = true;
    const linger noLinger{0, 0};
    if (!SetOption(listener_, SRTO_TRANSTYPE, live) ||
        !SetOption(listener_, SRTO_RCVSYN, synchronous) ||
        !SetOption(listener_, SRTO_REUSEADDR, synchronous) ||
        !SetOption(listener_, SRTO_RCVLATENCY, config.latencyMs) ||
        !SetOption(listener_, SRTO_LINGER, noLinger) ||
        !SetOption(listener_, SRTO_ENFORCEDENCRYPTION, enforceEncryption) ||
        (config.passphraseLength &&
         srt_setsockflag(listener_, SRTO_PASSPHRASE, config.passphrase.data(),
                         config.passphraseLength) == SRT_ERROR)) {
        ReportSocketError("Cannot configure the listener socket.");
        return false;
    }
    if (srt_bind(listener_, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) ==
            SRT_ERROR ||
        srt_listen(listener_, 1) == SRT_ERROR) {
        ReportSocketError("Cannot bind/listen on the configured address and port.");
        return false;
    }
    poll_ = srt_epoll_create();
    if (poll_ < 0 || srt_epoll_add_usock(poll_, listener_, &ReadEvents) == SRT_ERROR) {
        ReportSocketError("Cannot initialize the SRT event poller.");
        return false;
    }
    ResetEvent(stopped_.Get());
    try {
        worker_ = std::thread(&SrtReceiver::ReceiveLoop, this);
    } catch (...) {
        SetEvent(stopped_.Get());
        Report("Cannot create the receive thread.");
        return false;
    }
    return true;
}

bool SrtReceiver::Drain(SRTSOCKET socket) {
    std::array<BYTE, 65536> message{};
    while (WaitForSingleObject(stopped_.Get(), 0) == WAIT_TIMEOUT) {
        const int size = srt_recvmsg(socket, reinterpret_cast<char *>(message.data()),
                                     static_cast<int>(message.size()));
        if (size <= 0) {
            return size < 0 && srt_getlasterror(nullptr) == SRT_EASYNCRCV;
        }
        switch (buffer_.Push(message.data(), static_cast<std::size_t>(size))) {
        case PushResult::Success:
            break;
        case PushResult::InvalidLength:
            Report("Disconnecting: message length is not a multiple of 188 bytes.");
            return false;
        case PushResult::InvalidSync:
            Report("Disconnecting: invalid MPEG-TS synchronization byte.");
            return false;
        case PushResult::Full:
            Report("Disconnecting: the host did not drain the receive FIFO in time.");
            return false;
        }
    }
    return false;
}

void SrtReceiver::ReceiveLoop() noexcept {
    Socket client;
    try {
        while (WaitForSingleObject(stopped_.Get(), 0) == WAIT_TIMEOUT) {
            SRT_EPOLL_EVENT event;
            const int count = srt_epoll_uwait(poll_, &event, 1, 100);
            if (count == SRT_ERROR) {
                if (srt_getlasterror(nullptr) == SRT_ETIMEOUT) {
                    continue;
                }
                ReportSocketError("SRT event polling failed.");
                break;
            }
            if (!count) {
                continue;
            }
            if (!client.IsValid()) {
                client.Reset(srt_accept(listener_, nullptr, nullptr));
                if (!client.IsValid()) {
                    if (srt_getlasterror(nullptr) != SRT_EASYNCRCV) {
                        ReportSocketError("SRT accept failed.");
                        break;
                    }
                    continue;
                }
                const bool synchronous = false;
                if (!SetOption(client.Get(), SRTO_RCVSYN, synchronous)) {
                    client.Reset();
                    continue;
                }
                srt_epoll_remove_usock(poll_, listener_);
                if (srt_epoll_add_usock(poll_, client.Get(), &ReadEvents) == SRT_ERROR) {
                    ReportSocketError("Cannot monitor the accepted connection.");
                    break;
                }
                buffer_.Clear();
                continue;
            }
            if (!Drain(client.Get())) {
                srt_epoll_remove_usock(poll_, client.Get());
                client.Reset();
                if (srt_epoll_add_usock(poll_, listener_, &ReadEvents) == SRT_ERROR) {
                    ReportSocketError("Cannot resume listening.");
                    break;
                }
            }
        }
    } catch (...) {
        Report("The SRT receive worker stopped after an unexpected exception.");
    }
    if (client.IsValid()) {
        srt_epoll_remove_usock(poll_, client.Get());
        client.Reset();
    }
    SetEvent(stopped_.Get());
}

void SrtReceiver::Stop() noexcept {
    SetEvent(stopped_.Get());
    if (worker_.joinable()) {
        worker_.join();
    }
    if (listener_ != SRT_INVALID_SOCK) {
        srt_close(listener_);
        listener_ = SRT_INVALID_SOCK;
    }
    if (poll_ >= 0) {
        srt_epoll_release(poll_);
        poll_ = -1;
    }
    if (runtimeStarted_) {
        ReleaseRuntime();
        runtimeStarted_ = false;
    }
}

} // namespace bondriver
