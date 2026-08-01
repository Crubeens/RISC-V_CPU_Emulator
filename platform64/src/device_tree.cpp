#include "rv64/platform/device_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace rv64::platform {

namespace {

constexpr std::uint32_t fdt_magic = 0xD00DFEEDU;
constexpr std::uint32_t fdt_begin_node = 1U;
constexpr std::uint32_t fdt_end_node = 2U;
constexpr std::uint32_t fdt_property = 3U;
constexpr std::uint32_t fdt_nop = 4U;
constexpr std::uint32_t fdt_end = 9U;
constexpr std::size_t fdt_header_size = 40U;

[[nodiscard]] bool contains_range(
    std::size_t container_size,
    std::size_t offset,
    std::size_t size) noexcept
{
    return offset <= container_size &&
           size <= container_size - offset;
}

[[nodiscard]] std::uint32_t read_be32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::uint64_t read_be64(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept
{
    return (static_cast<std::uint64_t>(read_be32(bytes, offset)) << 32U) |
           read_be32(bytes, offset + 4U);
}

void write_be64(
    std::span<std::uint8_t> bytes,
    std::size_t offset,
    std::uint64_t value) noexcept
{
    for (std::size_t index = 0; index < 8U; ++index) {
        const unsigned int shift =
            static_cast<unsigned int>((7U - index) * 8U);
        bytes[offset + index] =
            static_cast<std::uint8_t>(value >> shift);
    }
}

[[nodiscard]] bool align_to_word(
    std::size_t value,
    std::size_t& aligned) noexcept
{
    if (value > std::numeric_limits<std::size_t>::max() - 3U) {
        return false;
    }
    aligned = (value + 3U) & ~std::size_t{3U};
    return true;
}

[[nodiscard]] bool read_c_string(
    std::span<const std::uint8_t> bytes,
    std::size_t offset,
    std::string_view& value,
    std::size_t& next) noexcept
{
    if (offset >= bytes.size()) {
        return false;
    }
    std::size_t end = offset;
    while (end < bytes.size() && bytes[end] != 0U) {
        ++end;
    }
    if (end == bytes.size()) {
        return false;
    }
    value = std::string_view(
        reinterpret_cast<const char*>(bytes.data() + offset),
        end - offset);
    next = end + 1U;
    return true;
}

[[nodiscard]] bool is_memory_node(std::string_view name) noexcept
{
    return name == "memory" || name.starts_with("memory@");
}

} // namespace

DeviceTreeMemoryPatchResult patch_device_tree_memory(
    std::span<std::uint8_t> blob,
    rv::PhysAddr expected_base,
    std::uint64_t size) noexcept
{
    const auto invalid = [] {
        return DeviceTreeMemoryPatchResult{
            DeviceTreeMemoryPatchError::InvalidBlob};
    };
    if (blob.size() < fdt_header_size ||
        read_be32(blob, 0U) != fdt_magic) {
        return invalid();
    }

    const std::size_t total_size = read_be32(blob, 4U);
    const std::size_t structure_offset = read_be32(blob, 8U);
    const std::size_t strings_offset = read_be32(blob, 12U);
    const std::size_t strings_size = read_be32(blob, 32U);
    const std::size_t structure_size = read_be32(blob, 36U);
    if (total_size > blob.size() || total_size < fdt_header_size ||
        !contains_range(total_size, structure_offset, structure_size) ||
        !contains_range(total_size, strings_offset, strings_size)) {
        return invalid();
    }

    auto structure = blob.subspan(structure_offset, structure_size);
    const auto strings = std::span<const std::uint8_t>(blob).subspan(
        strings_offset,
        strings_size);
    std::size_t cursor = 0U;
    std::size_t depth = 0U;
    std::size_t memory_depth = std::numeric_limits<std::size_t>::max();
    bool memory_found = false;

    while (contains_range(structure.size(), cursor, 4U)) {
        const std::uint32_t token = read_be32(structure, cursor);
        cursor += 4U;
        if (token == fdt_begin_node) {
            std::string_view name;
            std::size_t next = 0U;
            if (!read_c_string(structure, cursor, name, next)) {
                return invalid();
            }
            if (!align_to_word(next, next) || next > structure.size()) {
                return invalid();
            }
            ++depth;
            if (!memory_found && depth == 2U && is_memory_node(name)) {
                memory_found = true;
                memory_depth = depth;
            }
            cursor = next;
            continue;
        }
        if (token == fdt_end_node) {
            if (depth == 0U) {
                return invalid();
            }
            if (depth == memory_depth) {
                return {
                    DeviceTreeMemoryPatchError::MemoryRegNotFound};
            }
            --depth;
            continue;
        }
        if (token == fdt_property) {
            if (!contains_range(structure.size(), cursor, 8U)) {
                return invalid();
            }
            const std::size_t value_size = read_be32(structure, cursor);
            const std::size_t name_offset =
                read_be32(structure, cursor + 4U);
            cursor += 8U;
            if (!contains_range(structure.size(), cursor, value_size) ||
                name_offset >= strings.size()) {
                return invalid();
            }
            std::string_view property_name;
            std::size_t ignored = 0U;
            if (!read_c_string(
                    strings,
                    name_offset,
                    property_name,
                    ignored)) {
                return invalid();
            }
            if (depth == memory_depth && property_name == "reg") {
                if (value_size != 16U ||
                    read_be64(structure, cursor) != expected_base) {
                    return {
                        DeviceTreeMemoryPatchError::UnexpectedMemoryLayout};
                }
                write_be64(structure, cursor + 8U, size);
                return {};
            }
            std::size_t next = 0U;
            if (!align_to_word(cursor + value_size, next) ||
                next > structure.size()) {
                return invalid();
            }
            cursor = next;
            continue;
        }
        if (token == fdt_nop) {
            continue;
        }
        if (token == fdt_end) {
            return {
                memory_found
                    ? DeviceTreeMemoryPatchError::MemoryRegNotFound
                    : DeviceTreeMemoryPatchError::MemoryNodeNotFound};
        }
        return invalid();
    }
    return invalid();
}

const char* device_tree_memory_patch_error_message(
    DeviceTreeMemoryPatchError error) noexcept
{
    switch (error) {
    case DeviceTreeMemoryPatchError::None:
        return "no error";
    case DeviceTreeMemoryPatchError::InvalidBlob:
        return "invalid flattened device tree";
    case DeviceTreeMemoryPatchError::MemoryNodeNotFound:
        return "memory node not found";
    case DeviceTreeMemoryPatchError::MemoryRegNotFound:
        return "memory reg property not found";
    case DeviceTreeMemoryPatchError::UnexpectedMemoryLayout:
        return "memory reg is not the expected 64-bit base/size tuple";
    }
    return "unknown device-tree error";
}

} // namespace rv64::platform
