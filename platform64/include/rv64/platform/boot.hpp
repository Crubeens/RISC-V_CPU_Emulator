#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "rv/common/bus.hpp"
#include "rv64/platform/address_map.hpp"

namespace rv64::platform {

inline constexpr std::uint64_t kernel_load_offset =
    4ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t device_tree_alignment = 4096ULL;
inline constexpr std::uint64_t device_tree_firmware_padding =
    64ULL * 1024ULL;

struct BootConfig {
    std::span<const std::uint8_t> firmware;
    std::span<const std::uint8_t> kernel;
    std::span<const std::uint8_t> device_tree;
    std::uint64_t hart_id{};
};

struct BootLayout {
    rv::PhysAddr firmware_address{address_map::dram_base};
    rv::PhysAddr kernel_address{
        address_map::dram_base + kernel_load_offset};
    rv::PhysAddr device_tree_address{};
};

enum class BootError : std::uint8_t {
    None,
    MissingFirmware,
    MissingKernel,
    MissingDeviceTree,
    RamTooSmall,
    ImagesOverlap,
    ImageLoadFailed,
};

struct BootResult {
    BootError error{BootError::None};
    BootLayout layout{};

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return error == BootError::None;
    }
};

[[nodiscard]] constexpr std::string_view boot_error_message(
    BootError error) noexcept
{
    switch (error) {
    case BootError::None:
        return "no error";
    case BootError::MissingFirmware:
        return "firmware image is empty";
    case BootError::MissingKernel:
        return "kernel image is empty";
    case BootError::MissingDeviceTree:
        return "device tree image is empty";
    case BootError::RamTooSmall:
        return "RAM is too small for the fixed boot layout";
    case BootError::ImagesOverlap:
        return "boot images overlap in RAM";
    case BootError::ImageLoadFailed:
        return "boot image could not be loaded into RAM";
    }
    return "unknown boot error";
}

} // namespace rv64::platform
