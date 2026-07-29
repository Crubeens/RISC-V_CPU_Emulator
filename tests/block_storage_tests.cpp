#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rv/devices/block_storage.hpp"
#include "rv/devices/virtio_block.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

class TemporaryFile {
  public:
    explicit TemporaryFile(std::uint64_t size)
    {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("rv-block-storage-" + std::to_string(suffix) + ".img");

        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        if (!output || size == 0U) {
            throw std::runtime_error("cannot create block-storage test file");
        }
        output.seekp(static_cast<std::streamoff>(size - 1U));
        output.put('\0');
        output.flush();
        if (!output) {
            throw std::runtime_error("cannot size block-storage test file");
        }
    }

    ~TemporaryFile()
    {
        std::error_code error;
        static_cast<void>(std::filesystem::remove(path_, error));
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void test_memory_storage()
{
    rv::devices::MemoryBlockStorage storage(
        std::vector<std::uint8_t>(1024U));
    const std::array source{
        std::uint8_t{0x11},
        std::uint8_t{0x22},
        std::uint8_t{0x33},
    };
    CHECK(storage.size() == 1024U);
    CHECK(!storage.file_backed());
    CHECK(storage.write(510U, source));

    std::array<std::uint8_t, source.size()> destination{};
    CHECK(storage.read(510U, destination));
    CHECK(destination == source);
    CHECK(!storage.read(1023U, destination));
    CHECK(!storage.write(1023U, source));
    CHECK(storage.flush());
}

void test_file_storage_persists_without_full_image_copy()
{
    TemporaryFile file(1024U);
    const std::array source{
        std::uint8_t{0xA5},
        std::uint8_t{0x5A},
        std::uint8_t{0xC3},
        std::uint8_t{0x3C},
    };

    {
        rv::devices::FileBlockStorage storage(file.path());
        CHECK(storage.size() == 1024U);
        CHECK(storage.file_backed());
        CHECK(storage.write(508U, source));
        CHECK(storage.flush());
    }

    {
        rv::devices::FileBlockStorage storage(file.path());
        std::array<std::uint8_t, source.size()> destination{};
        CHECK(storage.read(508U, destination));
        CHECK(destination == source);
        CHECK(!storage.read(1022U, destination));
        CHECK(!storage.write(1022U, source));

        auto shared =
            std::make_shared<rv::devices::FileBlockStorage>(file.path());
        rv::devices::VirtioBlock device(0x10001000U, 0x1000U, shared);
        CHECK(device.file_backed());
        CHECK(device.storage_size() == 1024U);
        CHECK(device.disk_image().empty());
    }
}

void test_invalid_file_storage_is_rejected()
{
    bool missing_rejected = false;
    try {
        rv::devices::FileBlockStorage missing(
            std::filesystem::temp_directory_path() /
            "rv-block-storage-file-does-not-exist.img");
        static_cast<void>(missing);
    } catch (const std::runtime_error&) {
        missing_rejected = true;
    }
    CHECK(missing_rejected);

    TemporaryFile partial_sector(513U);
    bool partial_rejected = false;
    try {
        auto storage =
            std::make_shared<rv::devices::FileBlockStorage>(
                partial_sector.path());
        rv::devices::VirtioBlock device(
            0x10001000U,
            0x1000U,
            std::move(storage));
        static_cast<void>(device);
    } catch (const std::invalid_argument&) {
        partial_rejected = true;
    }
    CHECK(partial_rejected);
}

} // namespace

int main()
{
    test_memory_storage();
    test_file_storage_persists_without_full_image_copy();
    test_invalid_file_storage_is_rejected();

    if (failures != 0) {
        std::cerr << failures << " block storage test(s) failed\n";
        return 1;
    }
    std::cout << "All block storage tests passed\n";
    return 0;
}
