#include "Sha256.h"

#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace mzzplork {

namespace {

std::string ToHex(const std::vector<BYTE>& bytes) {
    std::ostringstream oss;
    for (BYTE b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

class Sha256Hasher {
public:
    Sha256Hasher() {
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
        }
        DWORD hashObjectSize = 0, dataLen = 0;
        BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjectSize), sizeof(DWORD), &dataLen, 0);
        hashObject_.resize(hashObjectSize);

        if (!NT_SUCCESS(BCryptCreateHash(algorithm_, &hash_, hashObject_.data(), static_cast<ULONG>(hashObject_.size()), nullptr, 0, 0))) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            throw std::runtime_error("BCryptCreateHash failed");
        }
    }

    ~Sha256Hasher() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }

    void Update(const unsigned char* data, size_t len) {
        BCryptHashData(hash_, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0);
    }

    std::string Finish() {
        DWORD hashLen = 0, dataLen = 0;
        BCryptGetProperty(algorithm_, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(DWORD), &dataLen, 0);
        std::vector<BYTE> digest(hashLen);
        BCryptFinishHash(hash_, digest.data(), hashLen, 0);
        return ToHex(digest);
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<BYTE> hashObject_;
};

}  // namespace

std::string Sha256Hex(const std::string& data) {
    Sha256Hasher hasher;
    hasher.Update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return hasher.Finish();
}

std::string Sha256HexOfFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open file for hashing: " + filePath);
    }
    Sha256Hasher hasher;
    std::vector<char> buffer(1 << 16);
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0) {
        hasher.Update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(file.gcount()));
    }
    return hasher.Finish();
}

}

