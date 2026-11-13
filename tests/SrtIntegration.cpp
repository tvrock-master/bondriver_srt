#include <srt.h>

#include "Config.h"
#include "Diagnostics.h"
#include "IBonDriver2.h"
#include "TsBuffer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace bondriver;
namespace fs = std::filesystem;

namespace {

void Require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Library {
  public:
    explicit Library(const fs::path &path) : handle_(LoadLibraryW(path.c_str())) {
        Require(handle_ != nullptr, "cannot load BonDriver DLL");
    }
    ~Library() { FreeLibrary(handle_); }
    IBonDriver2 *Create() const {
        const auto create =
            reinterpret_cast<IBonDriver *(*)()>(GetProcAddress(handle_, "CreateBonDriver"));
        Require(create != nullptr, "missing CreateBonDriver export");
        return static_cast<IBonDriver2 *>(create());
    }

  private:
    HMODULE handle_;
};

struct ReleaseDriver {
    void operator()(IBonDriver2 *driver) const { driver->Release(); }
};

class TestDirectory {
  public:
    TestDirectory() {
        path_ = fs::current_path() / (L"SRT integration " + std::to_wstring(GetCurrentProcessId()));
        Require(fs::create_directory(path_), "test directory already exists");
    }
    ~TestDirectory() {
        std::error_code error;
        fs::remove(path_ / L"BonDriver_SRT.ini", error);
        fs::remove(path_ / L"BonDriver_SRT.dll", error);
        fs::remove(path_, error);
    }
    const fs::path &Path() const { return path_; }
    void Settings(const char *passphrase) const {
        std::ofstream file(path_ / L"BonDriver_SRT.ini", std::ios::binary);
        file << "[SRT]\nBasePort=41320\nListenAddress=127.0.0.1\nLatencyMs=120\nPassphrase="
             << passphrase << '\n';
        Require(static_cast<bool>(file), "cannot write test settings");
    }

  private:
    fs::path path_;
};

SRTSOCKET Connect(const char *password) {
    const auto socket = srt_create_socket();
    const SRT_TRANSTYPE live = SRTT_LIVE;
    const int timeout = 1000;
    const int keyBytes = 32;
    if (!SetOption(socket, SRTO_TRANSTYPE, live) || !SetOption(socket, SRTO_CONNTIMEO, timeout) ||
        !SetOption(socket, SRTO_SNDTIMEO, timeout) ||
        (*password && (!SetOption(socket, SRTO_PBKEYLEN, keyBytes) ||
                       srt_setsockflag(socket, SRTO_PASSPHRASE, password,
                                       static_cast<int>(std::strlen(password))) == SRT_ERROR))) {
        srt_close(socket);
        return SRT_INVALID_SOCK;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(41320);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (srt_connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ==
        SRT_ERROR) {
        srt_close(socket);
        return SRT_INVALID_SOCK;
    }
    return socket;
}

void Transfer(IBonDriver2 &driver, const char *password, bool copyRead) {
    constexpr std::size_t MessageSize = 1316;
    std::vector<BYTE> expected(MessageSize * 512);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<BYTE>(i * 29 + i / TsPacketSize);
    }
    for (std::size_t i = 0; i < expected.size(); i += TsPacketSize) {
        expected[i] = 0x47;
    }
    const auto socket = Connect(password);
    Require(socket != SRT_INVALID_SOCK, "caller connection failed");
    std::atomic<bool> consumed{false};
    std::atomic<bool> sent{true};
    std::thread sender([&] {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t offset = 0; offset < expected.size() && !consumed; offset += MessageSize) {
            if (srt_sendmsg(socket, reinterpret_cast<const char *>(expected.data() + offset),
                            static_cast<int>(MessageSize), -1,
                            1) != static_cast<int>(MessageSize)) {
                sent = false;
                break;
            }
            std::this_thread::sleep_until(
                start + std::chrono::microseconds((offset / MessageSize + 1) * 500));
        }
    });

    std::vector<BYTE> received;
    std::array<BYTE, TsBlockSize> copy{};
    const auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && received.size() < expected.size()) {
        driver.WaitTsStream(100);
        BYTE *data = copy.data();
        DWORD size = 0;
        DWORD remaining = 0;
        const bool ok = copyRead ? driver.GetTsStream(data, &size, &remaining)
                                 : driver.GetTsStream(&data, &size, &remaining);
        if (!ok || size % TsPacketSize) {
            break;
        }
        received.insert(received.end(), data, data + size);
    }
    consumed = true;
    sender.join();
    srt_close(socket);
    Require(sent && received == expected, "received data differs from the sender");
    Sleep(150);
}

void CheckActiveClose(IBonDriver2 &driver, const char *password) {
    const auto socket = Connect(password);
    Require(socket != SRT_INVALID_SOCK, "active close connection failed");
    std::atomic<DWORD> wake{WAIT_FAILED};
    std::thread waiter([&] { wake = driver.WaitTsStream(0); });
    Sleep(30);
    driver.CloseTuner();
    waiter.join();
    srt_close(socket);
    Require(wake == WAIT_ABANDONED, "close did not wake an infinite receive wait");
    Require(driver.GetCurChannel() == InvalidChannel && !driver.IsTunerOpening(),
            "close did not release channel ownership");
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        return 2;
    }
    try {
        TestDirectory directory;
        const auto dll = directory.Path() / L"BonDriver_SRT.dll";
        fs::copy_file(argv[1], dll);
        Library library(dll);
        std::unique_ptr<IBonDriver2, ReleaseDriver> driver(library.Create());
        Require(driver && driver->OpenTuner(), "OpenTuner failed");
        directory.Settings("");
        Require(driver->SetChannel(0, 0), "configured listener failed");
        Require(!driver->SetChannel(1, 0) && !driver->SetChannel(0, 10),
                "invalid channel accepted");
        Require(driver->WaitTsStream(10) == WAIT_TIMEOUT, "empty FIFO signaled ready");
        driver->CloseTuner();

        Require(srt_startup() != SRT_ERROR, "sender runtime initialization failed");
        struct RuntimeCleanup {
            ~RuntimeCleanup() { srt_cleanup(); }
        } runtime;

        for (const char *password : {"", "integration-test-key"}) {
            directory.Settings(password);
            Require(driver->OpenTuner() && driver->SetChannel(0, 0), "configured listener failed");
            if (*password) {
                for (const char *wrong : {"", "incorrect-test-key"}) {
                    const auto rejected = Connect(wrong);
                    if (rejected != SRT_INVALID_SOCK) {
                        srt_close(rejected);
                        throw std::runtime_error("missing or incorrect passphrase accepted");
                    }
                }
            }
            Transfer(*driver, password, false);
            Transfer(*driver, password, true);
            driver->PurgeTsStream();
            Require(!driver->GetReadyCount(), "purge did not empty the FIFO");
            CheckActiveClose(*driver, password);
        }
        Require(driver->OpenTuner() &&
                    static_cast<IBonDriver *>(driver.get())->SetChannel(static_cast<BYTE>(14)) &&
                    driver->GetCurChannel() == 1,
                "legacy channel API or reopen failed");
        driver->CloseTuner();
        std::puts("PASS: plaintext/AES-256 byte equality, both read APIs, key rejection, "
                  "reconnect, active close/wait and reopen");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
