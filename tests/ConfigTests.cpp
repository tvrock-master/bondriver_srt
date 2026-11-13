#include "Config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace bondriver;

int main() {
    const auto path = std::filesystem::current_path() /
                      (L"settings-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    bool ownsFile = false;
    try {
        if (std::filesystem::exists(path)) {
            throw std::runtime_error("test INI already exists");
        }
        Config defaults;
        std::string error;
        if (!ReadConfig(path.wstring(), defaults, error) || defaults.basePort != 3230 ||
            defaults.latencyMs != 120 || defaults.passphraseLength != 0) {
            throw std::runtime_error("defaults without an INI");
        }
        for (const char *input : {"BasePort=abc", "BasePort=0", "BasePort=65527", "LatencyMs=-1",
                                  "LatencyMs=60001", "Passphrase=short"}) {
            {
                std::ofstream file(path, std::ios::binary);
                if (!file) {
                    throw std::runtime_error("cannot create test settings");
                }
                ownsFile = true;
                file << "[SRT]\n" << input << '\n';
            }
            Config config;
            if (ReadConfig(path.wstring(), config, error) || error.empty()) {
                throw std::runtime_error("malformed settings were accepted");
            }
        }
        {
            std::ofstream file(path, std::ios::binary);
            file << "[SRT]\nBasePort=65526\nLatencyMs=60000\nPassphrase=test-secret-value\n";
        }
        Config valid;
        if (!ReadConfig(path.wstring(), valid, error) || valid.basePort != 65526 ||
            valid.latencyMs != 60000 || valid.passphraseLength != 17) {
            throw std::runtime_error("valid custom settings");
        }
        if (ownsFile) {
            std::filesystem::remove(path);
        }
        std::puts("PASS: defaults, strict numeric parsing and passphrase validation");
        return 0;
    } catch (const std::exception &error) {
        // Only the unique file owned by this test is ever removed.
        if (ownsFile) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
