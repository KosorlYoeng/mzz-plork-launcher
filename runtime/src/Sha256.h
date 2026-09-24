#pragma once
#include <string>

namespace mzzplork {

// Real SHA-256 via Windows CNG (bcrypt.dll) -- not a hand-rolled
// implementation. Using a vetted OS-provided primitive instead of custom
// crypto code, consistent with not having a package manager available to
// pull in a library like OpenSSL.
std::string Sha256Hex(const std::string& data);
std::string Sha256HexOfFile(const std::string& filePath);

}
