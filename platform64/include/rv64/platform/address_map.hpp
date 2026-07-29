#pragma once

#include <cstdint>

#include "rv/common/bus.hpp"

namespace rv64::platform::address_map {

inline constexpr rv::PhysAddr clint_base = 0x02000000ULL;
inline constexpr std::uint64_t clint_size = 0x00010000ULL;

inline constexpr rv::PhysAddr rtc_base = 0x00101000ULL;
inline constexpr std::uint64_t rtc_size = 0x00001000ULL;
inline constexpr std::uint32_t rtc_irq = 11;

inline constexpr rv::PhysAddr plic_base = 0x0C000000ULL;
inline constexpr std::uint64_t plic_size = 0x04000000ULL;

inline constexpr rv::PhysAddr uart_base = 0x10000000ULL;
inline constexpr std::uint64_t uart_size = 0x00000100ULL;
inline constexpr std::uint32_t uart_irq = 10;

inline constexpr rv::PhysAddr virtio_block_base = 0x10001000ULL;
inline constexpr std::uint64_t virtio_block_size = 0x00001000ULL;
inline constexpr std::uint32_t virtio_block_irq = 1;

inline constexpr rv::PhysAddr virtio_net_base = 0x10002000ULL;
inline constexpr std::uint64_t virtio_net_size = 0x00001000ULL;
inline constexpr std::uint32_t virtio_net_irq = 2;

inline constexpr rv::PhysAddr syscon_base = 0x11100000ULL;
inline constexpr std::uint64_t syscon_size = 0x00001000ULL;

inline constexpr rv::PhysAddr framebuffer_base = 0x40000000ULL;

inline constexpr rv::PhysAddr dram_base = 0x80000000ULL;
inline constexpr std::uint64_t default_dram_size =
    256ULL * 1024ULL * 1024ULL;

} // namespace rv64::platform::address_map
