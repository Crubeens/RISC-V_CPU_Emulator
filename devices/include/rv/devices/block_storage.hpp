#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace rv::devices {

class BlockStorage {
  public:
    virtual ~BlockStorage() = default;

    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    [[nodiscard]] virtual bool read(
        std::uint64_t offset,
        std::span<std::uint8_t> destination) noexcept = 0;
    [[nodiscard]] virtual bool write(
        std::uint64_t offset,
        std::span<const std::uint8_t> source) noexcept = 0;
    [[nodiscard]] virtual bool flush() noexcept = 0;
    [[nodiscard]] virtual bool file_backed() const noexcept = 0;
};

class MemoryBlockStorage final : public BlockStorage {
  public:
    explicit MemoryBlockStorage(std::vector<std::uint8_t> bytes);
    explicit MemoryBlockStorage(std::uint64_t size);

    [[nodiscard]] std::uint64_t size() const noexcept override;
    [[nodiscard]] bool read(
        std::uint64_t offset,
        std::span<std::uint8_t> destination) noexcept override;
    [[nodiscard]] bool write(
        std::uint64_t offset,
        std::span<const std::uint8_t> source) noexcept override;
    [[nodiscard]] bool flush() noexcept override;
    [[nodiscard]] bool file_backed() const noexcept override;

    [[nodiscard]] std::span<std::uint8_t> bytes() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

  private:
    std::vector<std::uint8_t> bytes_;
};

class FileBlockStorage final : public BlockStorage {
  public:
    explicit FileBlockStorage(std::filesystem::path path);

    [[nodiscard]] std::uint64_t size() const noexcept override;
    [[nodiscard]] bool read(
        std::uint64_t offset,
        std::span<std::uint8_t> destination) noexcept override;
    [[nodiscard]] bool write(
        std::uint64_t offset,
        std::span<const std::uint8_t> source) noexcept override;
    [[nodiscard]] bool flush() noexcept override;
    [[nodiscard]] bool file_backed() const noexcept override;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

  private:
    std::filesystem::path path_;
    std::fstream file_;
    std::uint64_t size_{};
};

} // namespace rv::devices
