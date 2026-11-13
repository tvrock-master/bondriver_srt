#pragma once

#include <array>
#include <string>
#include <windows.h>

namespace bondriver {

constexpr DWORD ChannelCount = 10;
constexpr DWORD InvalidChannel = 0xFFFFFFFF;

struct Config {
    std::wstring listenAddress = L"127.0.0.1";
    unsigned basePort = 3230;
    int latencyMs = 120;
    std::array<char, 80> passphrase{};
    int passphraseLength = 0;

    Config() = default;
    ~Config() { SecureZeroMemory(passphrase.data(), passphrase.size()); }
    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;
};

std::wstring ConfigurationPath(HMODULE module);
bool ReadConfig(const std::wstring &path, Config &config, std::string &error);

} // namespace bondriver
