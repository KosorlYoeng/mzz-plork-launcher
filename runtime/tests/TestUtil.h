#pragma once
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

// Test temp directories are left on disk under the OS temp folder rather
// than deleted here -- they are small, randomly named, and isolated.
namespace mzzplork::test {

inline std::string MakeTempAppData() {
    std::random_device rd;
    const char* base = std::getenv("TEMP");
    std::string root = base ? base : "C:\\Temp";
    std::string dir = root + "\\mzzplork-test-" + std::to_string(rd());
    std::filesystem::create_directories(dir + "\\logs");
    std::filesystem::create_directories(dir + "\\config");
    std::filesystem::create_directories(dir + "\\cache");
    std::filesystem::create_directories(dir + "\\runtime");
    std::filesystem::create_directories(dir + "\\resources");
    return dir;
}

}
