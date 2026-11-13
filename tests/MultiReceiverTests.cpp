#include <srt.h>
#include <ws2tcpip.h>

#include "IBonDriver2.h"
#include "WinHandle.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using bondriver::WinHandle;
constexpr unsigned BasePort = 42320;
constexpr size_t MessageSize = 1316;
constexpr size_t DataSize = MessageSize * 128;

void Require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct ReleaseDriver {
    void operator()(IBonDriver2 *driver) const { driver->Release(); }
};
using Driver = std::unique_ptr<IBonDriver2, ReleaseDriver>;

class Library {
  public:
    explicit Library(const fs::path &path) : handle_(LoadLibraryW(path.c_str())) {
        Require(handle_ != nullptr, "load test DLL");
    }
    ~Library() { FreeLibrary(handle_); }
    Driver Create() const {
        auto create =
            reinterpret_cast<IBonDriver *(*)()>(GetProcAddress(handle_, "CreateBonDriver"));
        Require(create != nullptr, "factory export");
        Driver driver(static_cast<IBonDriver2 *>(create()));
        Require(driver && driver->OpenTuner(), "create/open receiver");
        return driver;
    }

  private:
    HMODULE handle_;
};

struct Directory {
    fs::path path =
        fs::current_path() / (L"MultiReceiver-" + std::to_wstring(GetCurrentProcessId()));
    Directory() { Require(fs::create_directory(path), "create isolated test directory"); }
    ~Directory() {
        std::error_code error;
        fs::remove(path / L"BonDriver_SRT.dll", error);
        fs::remove(path / L"BonDriver_SRT.ini", error);
        fs::remove(path, error);
    }
    void Settings(const char *secret, const char *address = "127.0.0.1") const {
        std::ofstream ini(path / L"BonDriver_SRT.ini");
        ini << "[SRT]\nBasePort=" << BasePort << "\nListenAddress=" << address
            << "\nLatencyMs=120\nPassphrase=" << secret << '\n';
        Require(static_cast<bool>(ini), "write test configuration");
    }
};

struct Socket {
    SRTSOCKET value;
    explicit Socket(SRTSOCKET socket) : value(socket) {}
    Socket(const Socket &) = delete;
    Socket(Socket &&other) noexcept : value(std::exchange(other.value, SRT_INVALID_SOCK)) {}
    ~Socket() {
        if (value != SRT_INVALID_SOCK) {
            srt_close(value);
        }
    }
};

Socket Connect(unsigned port, const char *secret, const char *ip) {
    Socket socket(srt_create_socket());
    Require(socket.value != SRT_INVALID_SOCK, "create caller");
    const SRT_TRANSTYPE live = SRTT_LIVE;
    const int timeout = 2000, key = 32;
    Require(
        srt_setsockflag(socket.value, SRTO_TRANSTYPE, &live, sizeof(live)) != SRT_ERROR &&
            srt_setsockflag(socket.value, SRTO_CONNTIMEO, &timeout, sizeof(timeout)) != SRT_ERROR &&
            srt_setsockflag(socket.value, SRTO_SNDTIMEO, &timeout, sizeof(timeout)) != SRT_ERROR,
        "configure caller");
    if (*secret) {
        Require(srt_setsockflag(socket.value, SRTO_PBKEYLEN, &key, sizeof(key)) != SRT_ERROR &&
                    srt_setsockflag(socket.value, SRTO_PASSPHRASE, secret,
                                    static_cast<int>(strlen(secret))) != SRT_ERROR,
                "configure encryption");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    Require(InetPtonA(AF_INET, ip, &address.sin_addr) == 1, "test caller address");
    Require(srt_connect(socket.value, reinterpret_cast<const sockaddr *>(&address),
                        sizeof(address)) != SRT_ERROR,
            "connect caller");
    return socket;
}

std::vector<BYTE> Data(BYTE marker) {
    std::vector<BYTE> data(DataSize, marker);
    for (size_t i = 0; i < data.size(); i += 188) {
        data[i] = 0x47;
    }
    return data;
}

bool Send(SRTSOCKET socket, const std::vector<BYTE> &data) {
    for (size_t pos = 0; pos < data.size(); pos += MessageSize) {
        if (srt_sendmsg(socket, reinterpret_cast<const char *>(data.data() + pos), MessageSize, -1,
                        1) != MessageSize) {
            return false;
        }
        Sleep(1);
    }
    return true;
}

void Read(IBonDriver2 &driver, std::vector<BYTE> &bytes) {
    driver.WaitTsStream(10);
    BYTE *data = nullptr;
    DWORD size = 0, remaining = 0;
    Require(driver.GetTsStream(&data, &size, &remaining), "read receiver");
    if (size) {
        bytes.insert(bytes.end(), data, data + size);
    }
}

void TransferPair(IBonDriver2 &a, unsigned portA, IBonDriver2 &b, unsigned portB,
                  const char *secret, const char *ipB = "127.0.0.1") {
    {
        auto socketA = Connect(portA, secret, "127.0.0.1");
        auto socketB = Connect(portB, secret, ipB);
        const auto expectedA = Data(0xa1), expectedB = Data(0xb2);
        auto senderA =
            std::async(std::launch::async, [&] { return Send(socketA.value, expectedA); });
        auto senderB =
            std::async(std::launch::async, [&] { return Send(socketB.value, expectedB); });
        std::vector<BYTE> gotA, gotB;
        const auto deadline = GetTickCount64() + 8000;
        while (GetTickCount64() < deadline && (gotA.size() < DataSize || gotB.size() < DataSize)) {
            Read(a, gotA);
            Read(b, gotB);
        }
        Require(senderA.get() && senderB.get(), "send concurrent streams");
        Require(gotA == expectedA && gotB == expectedB, "concurrent streams mixed or lost TS data");
    }
    Sleep(200);
}

void TuneEventually(IBonDriver2 &driver, DWORD channel) {
    Require(driver.OpenTuner() != FALSE, "reopen receiver");
    const auto deadline = GetTickCount64() + 3000;
    while (!driver.SetChannel(0, channel)) {
        Require(GetTickCount64() < deadline, "closed endpoint was not released");
        Sleep(50);
    }
}

void ProbeProcess(const fs::path &dll, DWORD channel, bool allowed) {
    std::array<wchar_t, 32768> executable{};
    Require(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) >
                0,
            "test executable path");
    std::wstring command = L"\"" + std::wstring(executable.data()) + L"\" --probe \"" +
                           dll.wstring() + L"\" " + std::to_wstring(channel) +
                           (allowed ? L" 1" : L" 0");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Require(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE,
            "start cross-process receiver probe");
    WinHandle child(process.hProcess), thread(process.hThread);
    if (WaitForSingleObject(child.Get(), 10000) != WAIT_OBJECT_0) {
        TerminateProcess(child.Get(), 2);
        WaitForSingleObject(child.Get(), 1000);
        throw std::runtime_error("cross-process receiver probe timed out");
    }
    DWORD exit = 1;
    Require(GetExitCodeProcess(child.Get(), &exit) && exit == 0,
            "cross-process ownership mismatch");
}

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 5 && wcscmp(argv[1], L"--probe") == 0) {
            Library library(argv[2]);
            auto driver = library.Create();
            const bool opened = driver->SetChannel(0, std::stoul(argv[3])) != FALSE;
            Require(opened == (wcscmp(argv[4], L"1") == 0), "child listener ownership");
            Require((driver->IsTunerOpening() != FALSE) == opened, "child opening state");
            return 0;
        }
        if (argc != 2) {
            return 2;
        }
        Require(srt_startup() != SRT_ERROR, "initialize caller runtime");
        struct Cleanup {
            ~Cleanup() { srt_cleanup(); }
        } cleanup;
        Directory directory;
        const auto dll = directory.path / L"BonDriver_SRT.dll";
        fs::copy_file(argv[1], dll);
        Library library(dll);
        auto a = library.Create(), b = library.Create(), c = library.Create();
        Require(a.get() != b.get() && a.get() != c.get(),
                "factory must return independent instances");
        for (const char *secret : {"", "multi-receiver-test-key"}) {
            directory.Settings(secret);
            TuneEventually(*a, 0);
            TuneEventually(*b, 1);
            Require(!c->SetChannel(0, 0) && !c->IsTunerOpening(), "duplicate endpoint accepted");
            ProbeProcess(dll, 0, false);
            ProbeProcess(dll, 2, true);
            puts("Concurrent separate ports");
            TransferPair(*a, BasePort, *b, BasePort + 1, secret);
            a->CloseTuner();
            Require(!a->IsTunerOpening() && b->IsTunerOpening(), "close affected another receiver");
            TuneEventually(*c, 2);
            puts("Other receiver survives close");
            TransferPair(*b, BasePort + 1, *c, BasePort + 2, secret);
            TuneEventually(*a, 0);
            c->CloseTuner();
            directory.Settings(secret, "0.0.0.0");
            Require(c->OpenTuner() != FALSE, "open wildcard collision probe");
            Require(!c->SetChannel(0, 1), "wildcard overlapped a specific address");
            TuneEventually(*c, 2);
            directory.Settings(secret);
            auto clash = library.Create();
            Require(!clash->SetChannel(0, 2), "specific address overlapped a wildcard");
            clash->CloseTuner();
            c->CloseTuner();
            directory.Settings(secret, "127.0.0.2");
            TuneEventually(*c, 1);
            puts("Different addresses, same port");
            TransferPair(*b, BasePort + 1, *c, BasePort + 1, secret, "127.0.0.2");
            a->CloseTuner();
            b->CloseTuner();
            c->CloseTuner();
        }
        puts("PASS: plaintext/AES-256 concurrent TS isolation, independent instances/close, "
             "endpoint reuse, cross-process exclusion and wildcard/address binding");
        return 0;
    } catch (const std::exception &error) {
        fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
