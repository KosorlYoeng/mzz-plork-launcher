#include <fstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "../src/Sha256.h"

using mzzplork::Sha256Hex;
using mzzplork::Sha256HexOfFile;

TEST_CASE(Sha256_KnownVectorForEmptyString) {
    // Well-known SHA-256 of the empty string.
    ASSERT_EQ(Sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST_CASE(Sha256_KnownVectorForAbc) {
    // Well-known SHA-256("abc") test vector (FIPS 180-2).
    ASSERT_EQ(Sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST_CASE(Sha256_OfFileMatchesSha256OfItsContent) {
    std::string dir = mzzplork::test::MakeTempAppData();
    std::string path = dir + "\\hashme.bin";
    std::ofstream out(path, std::ios::binary);
    out << "hello world";
    out.close();

    ASSERT_EQ(Sha256HexOfFile(path), Sha256Hex("hello world"));
}
