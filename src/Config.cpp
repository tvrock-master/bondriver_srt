#include "Config.h"

#include <charconv>
#include <vector>

namespace bondriver {
namespace {

bool ReadNumber(const std::wstring &path, const wchar_t *key, const wchar_t *fallback,
                unsigned minimum, unsigned maximum, unsigned &value) {
    std::array<wchar_t, 32> text{};
    const DWORD length = GetPrivateProfileStringW(L"SRT", key, fallback, text.data(),
                                                  static_cast<DWORD>(text.size()), path.c_str());
    if (!length || length >= text.size() - 1) {
        return false;
    }
    std::array<char, 32> digits{};
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') {
            return false;
        }
        digits[i] = static_cast<char>(text[i]);
    }
    const auto result = std::from_chars(digits.data(), digits.data() + length, value);
    return result.ec == std::errc{} && result.ptr == digits.data() + length && value >= minimum &&
           value <= maximum;
}

struct SecretText {
    std::array<wchar_t, 128> value{};
    ~SecretText() { SecureZeroMemory(value.data(), sizeof(value)); }
};

} // namespace

std::wstring ConfigurationPath(HMODULE module) {
    std::vector<wchar_t> path(512);
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length) {
            return {};
        }
        if (length < path.size()) {
            std::wstring result(path.data(), length);
            const auto separator = result.find_last_of(L"\\/");
            const auto extension = result.find_last_of(L'.');
            if (extension != std::wstring::npos &&
                (separator == std::wstring::npos || extension > separator)) {
                result.resize(extension);
            }
            return result + L".ini";
        }
        if (path.size() >= 32768) {
            return {};
        }
        path.resize(path.size() * 2);
    }
}

bool ReadConfig(const std::wstring &path, Config &config, std::string &error) {
    std::array<wchar_t, 64> address{};
    const DWORD addressLength =
        GetPrivateProfileStringW(L"SRT", L"ListenAddress", L"127.0.0.1", address.data(),
                                 static_cast<DWORD>(address.size()), path.c_str());
    if (!addressLength || addressLength >= address.size() - 1) {
        error = "ListenAddress is empty or too long.";
        return false;
    }
    config.listenAddress.assign(address.data(), addressLength);
    unsigned latency = 0;
    if (!ReadNumber(path, L"BasePort", L"3230", 1, 65536 - ChannelCount, config.basePort)) {
        error = "BasePort must be an integer from 1 to 65526.";
        return false;
    }
    if (!ReadNumber(path, L"LatencyMs", L"120", 0, 60000, latency)) {
        error = "LatencyMs must be an integer from 0 to 60000.";
        return false;
    }
    config.latencyMs = static_cast<int>(latency);

    SecretText secret;
    const DWORD length =
        GetPrivateProfileStringW(L"SRT", L"Passphrase", L"", secret.value.data(),
                                 static_cast<DWORD>(secret.value.size()), path.c_str());
    if (length && (length < 10 || length > 79)) {
        error = "Passphrase must be empty or contain 10 to 79 printable ASCII characters.";
        return false;
    }
    SecureZeroMemory(config.passphrase.data(), config.passphrase.size());
    for (DWORD i = 0; i < length; ++i) {
        if (secret.value[i] < 32 || secret.value[i] > 126) {
            error = "Passphrase contains a non-ASCII or control character.";
            return false;
        }
        config.passphrase[i] = static_cast<char>(secret.value[i]);
    }
    config.passphraseLength = static_cast<int>(length);
    error.clear();
    return true;
}

} // namespace bondriver
