#include "CacheService.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace mzzplork {

namespace fs = std::filesystem;

namespace {
std::string SanitizeKey(const std::string& key) {
    std::string safe;
    safe.reserve(key.size());
    for (char c : key) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            safe += c;
        } else {
            safe += '_';
        }
    }
    return safe.empty() ? "_" : safe;
}
}

ServiceResult FileCacheService::Initialize(const std::string& appDataRoot) {
    cacheDir_ = appDataRoot + "\\cache";
    std::error_code ec;
    fs::create_directories(cacheDir_, ec);
    if (ec) {
        return ServiceResult::Failed("could not create cache directory: " + ec.message());
    }
    return ServiceResult::Ok("cache ready at " + cacheDir_);
}

std::string FileCacheService::PathFor(const std::string& key) const {
    return cacheDir_ + "\\" + SanitizeKey(key) + ".cache";
}

bool FileCacheService::Has(const std::string& key) const {
    return fs::exists(PathFor(key));
}

bool FileCacheService::Put(const std::string& key, const std::string& data) {
    std::ofstream out(PathFor(key), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

std::optional<std::string> FileCacheService::Get(const std::string& key) const {
    std::ifstream in(PathFor(key), std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool FileCacheService::Remove(const std::string& key) {
    std::error_code ec;
    return fs::remove(PathFor(key), ec);
}

}
