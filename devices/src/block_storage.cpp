#include "rv/devices/block_storage.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace rv::devices {

namespace {

[[nodiscard]] bool in_bounds(
    std::uint64_t storage_size,
    std::uint64_t offset,
    std::size_t length) noexcept
{
    const auto requested = static_cast<std::uint64_t>(length);
    return offset <= storage_size &&
           requested <= storage_size - offset;
}

[[nodiscard]] bool representable_stream_offset(
    std::uint64_t offset) noexcept
{
    return offset <= static_cast<std::uint64_t>(
                         std::numeric_limits<std::streamoff>::max());
}

} // namespace

MemoryBlockStorage::MemoryBlockStorage(
    std::vector<std::uint8_t> bytes)
    : bytes_(std::move(bytes))
{
}

MemoryBlockStorage::MemoryBlockStorage(std::uint64_t size)
{
    if (size > static_cast<std::uint64_t>(
                   std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("memory block storage is too large");
    }
    bytes_.resize(static_cast<std::size_t>(size));
}

std::uint64_t MemoryBlockStorage::size() const noexcept
{
    return static_cast<std::uint64_t>(bytes_.size());
}

bool MemoryBlockStorage::read(
    std::uint64_t offset,
    std::span<std::uint8_t> destination) noexcept
{
    if (!in_bounds(size(), offset, destination.size())) {
        return false;
    }
    const auto begin =
        bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
    std::copy_n(begin, destination.size(), destination.begin());
    return true;
}

bool MemoryBlockStorage::write(
    std::uint64_t offset,
    std::span<const std::uint8_t> source) noexcept
{
    if (!in_bounds(size(), offset, source.size())) {
        return false;
    }
    auto destination =
        bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
    std::copy(source.begin(), source.end(), destination);
    return true;
}

bool MemoryBlockStorage::flush() noexcept
{
    return true;
}

bool MemoryBlockStorage::file_backed() const noexcept
{
    return false;
}

std::span<std::uint8_t> MemoryBlockStorage::bytes() noexcept
{
    return bytes_;
}

std::span<const std::uint8_t> MemoryBlockStorage::bytes() const noexcept
{
    return bytes_;
}

FileBlockStorage::FileBlockStorage(std::filesystem::path path)
    : path_(std::move(path))
{
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path_, error);
    if (error) {
        throw std::runtime_error(
            "cannot inspect block storage file: " + error.message());
    }
    if (file_size > std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("block storage file is too large");
    }
    size_ = static_cast<std::uint64_t>(file_size);
    file_.open(
        path_,
        std::ios::binary | std::ios::in | std::ios::out);
    if (!file_) {
        throw std::runtime_error(
            "cannot open block storage file for read-write access");
    }
}

std::uint64_t FileBlockStorage::size() const noexcept
{
    return size_;
}

bool FileBlockStorage::read(
    std::uint64_t offset,
    std::span<std::uint8_t> destination) noexcept
{
    if (!in_bounds(size_, offset, destination.size()) ||
        !representable_stream_offset(offset) ||
        destination.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    if (destination.empty()) {
        return true;
    }

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file_.read(
        reinterpret_cast<char*>(destination.data()),
        static_cast<std::streamsize>(destination.size()));
    return file_.good() ||
           file_.gcount() ==
               static_cast<std::streamsize>(destination.size());
}

bool FileBlockStorage::write(
    std::uint64_t offset,
    std::span<const std::uint8_t> source) noexcept
{
    if (!in_bounds(size_, offset, source.size()) ||
        !representable_stream_offset(offset) ||
        source.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    if (source.empty()) {
        return true;
    }

    file_.clear();
    file_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    file_.write(
        reinterpret_cast<const char*>(source.data()),
        static_cast<std::streamsize>(source.size()));
    return file_.good();
}

bool FileBlockStorage::flush() noexcept
{
    file_.flush();
    return file_.good();
}

bool FileBlockStorage::file_backed() const noexcept
{
    return true;
}

const std::filesystem::path& FileBlockStorage::path() const noexcept
{
    return path_;
}

} // namespace rv::devices
