#pragma once

#include <cstdint>
#include <span>

#include "rv/common/bus.hpp"

namespace rv64::platform {

enum class DeviceTreeMemoryPatchError : std::uint8_t {
    None,
    InvalidBlob,
    MemoryNodeNotFound,
    MemoryRegNotFound,
    UnexpectedMemoryLayout,
};

struct DeviceTreeMemoryPatchResult {
    DeviceTreeMemoryPatchError error{DeviceTreeMemoryPatchError::None};

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return error == DeviceTreeMemoryPatchError::None;
    }
};

// Updates the first top-level memory node's 64-bit <base, size> reg tuple.
// The parser deliberately supports only the layout emitted by the RV64
// platform DTS so malformed or incompatible blobs fail deterministically.
[[nodiscard]] DeviceTreeMemoryPatchResult patch_device_tree_memory(
    std::span<std::uint8_t> blob,
    rv::PhysAddr expected_base,
    std::uint64_t size) noexcept;

[[nodiscard]] const char* device_tree_memory_patch_error_message(
    DeviceTreeMemoryPatchError error) noexcept;

} // namespace rv64::platform
