#pragma once

#include <cstdint>

#include "rv32/core/bus.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

enum class MemoryAccessType : std::uint8_t {
    InstructionFetch,
    Load,
    Store,
};

enum class TranslationStatus : std::uint8_t {
    Ready,
    PageFault,
    AccessFault,
};

struct TranslationResult {
    TranslationStatus status{TranslationStatus::AccessFault};
    PhysAddr physical_address{};
    BusFault bus_fault{BusFault::None};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == TranslationStatus::Ready;
    }
};

namespace satp_bits {

inline constexpr std::uint32_t mode = 1U << 31U;
inline constexpr std::uint32_t asid = 0x1FFU << 22U;
inline constexpr std::uint32_t ppn = 0x003FFFFFU;

} // namespace satp_bits

namespace sv32_pte_bits {

inline constexpr std::uint32_t valid = 1U << 0U;
inline constexpr std::uint32_t read = 1U << 1U;
inline constexpr std::uint32_t write = 1U << 2U;
inline constexpr std::uint32_t execute = 1U << 3U;
inline constexpr std::uint32_t user = 1U << 4U;
inline constexpr std::uint32_t global = 1U << 5U;
inline constexpr std::uint32_t accessed = 1U << 6U;
inline constexpr std::uint32_t dirty = 1U << 7U;
inline constexpr std::uint32_t rsw = 0x3U << 8U;
inline constexpr std::uint32_t ppn = 0x003FFFFFU << 10U;

} // namespace sv32_pte_bits

// This implementation exposes Sv32 and no hardware ASIDs. Bare writes are
// normalized to zero; Sv32 writes retain MODE and the 22-bit root PPN.
[[nodiscard]] std::uint32_t sanitize_satp(
    std::uint32_t value) noexcept;

[[nodiscard]] PrivilegeMode effective_privilege(
    const CpuSnapshot& state,
    MemoryAccessType access) noexcept;

// Translates one naturally aligned RV32 access. The first MMU version has no
// TLB, so every translated access performs a fresh two-level page-table walk.
[[nodiscard]] TranslationResult translate_address(
    CpuBus& bus,
    const CpuSnapshot& state,
    std::uint32_t virtual_address,
    MemoryAccessType access);

} // namespace rv32
