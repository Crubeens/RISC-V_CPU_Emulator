#pragma once

#include <cstdint>

#include "rv32/core/bus.hpp"

namespace rv32::platform::address_map {

inline constexpr PhysAddr clint_base = 0x02000000ULL;
inline constexpr std::uint64_t clint_size = 0x00010000ULL;

inline constexpr PhysAddr plic_base = 0x0C000000ULL;
inline constexpr std::uint64_t plic_size = 0x04000000ULL;

inline constexpr PhysAddr uart_base = 0x10000000ULL;
inline constexpr std::uint64_t uart_size = 0x00000100ULL;
inline constexpr std::uint32_t uart_irq = 10;

inline constexpr PhysAddr virtio_block_base = 0x10001000ULL;
inline constexpr std::uint64_t virtio_block_size = 0x00001000ULL;
inline constexpr std::uint32_t virtio_block_irq = 1;

inline constexpr PhysAddr syscon_base = 0x11100000ULL;
inline constexpr std::uint64_t syscon_size = 0x00001000ULL;

inline constexpr PhysAddr framebuffer_base = 0x40000000ULL;

inline constexpr PhysAddr dram_base = 0x80000000ULL;
inline constexpr std::uint64_t default_dram_size =
    64ULL * 1024ULL * 1024ULL;

inline constexpr std::uint32_t default_reset_pc =
    static_cast<std::uint32_t>(dram_base);

} // namespace rv32::platform::address_map
