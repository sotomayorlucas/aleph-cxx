#include "doctest.h"
#include <cstdio>
#include <filesystem>
#include <string>
import aleph.io;

using namespace aleph::io;

TEST_CASE("MappedFile: open existing, expose bytes()") {
    const auto path = (std::filesystem::temp_directory_path()
                       / "aleph_test_mmap.bin").string();
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        const char payload[] = "abc123";
        std::fwrite(payload, 1, sizeof(payload) - 1, f);
        std::fclose(f);
    }

    auto r = MappedFile::open_read(path);
    REQUIRE(r);
    const auto bytes = r->bytes();
    CHECK(bytes.size() == 6);
    CHECK(static_cast<char>(bytes[0]) == 'a');
    CHECK(static_cast<char>(bytes[5]) == '3');

    std::remove(path.c_str());
}

TEST_CASE("MappedFile: error on missing path") {
    auto r = MappedFile::open_read("/definitely/does/not/exist.bin");
    CHECK_FALSE(r);
}
