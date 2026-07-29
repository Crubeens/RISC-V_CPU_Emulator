#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

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

struct MmuPerformanceCounters {
    std::uint64_t translations{};
    std::uint64_t bare_translations{};
    std::uint64_t tlb_hits{};
    std::uint64_t tlb_misses{};
    std::uint64_t page_table_walks{};
    std::uint64_t pte_reads{};
    std::uint64_t pte_writes{};
    std::uint64_t page_faults{};
    std::uint64_t access_faults{};
};

// RV32 satp exposes the architecturally defined nine-bit ASID field. Bare
// writes are normalized to zero; Sv32 writes retain MODE, ASID, and root PPN.
[[nodiscard]] std::uint32_t sanitize_satp(
    std::uint32_t value) noexcept;

[[nodiscard]] PrivilegeMode effective_privilege(
    const CpuSnapshot& state,
    MemoryAccessType access) noexcept;

// Reference translation path. It intentionally performs a fresh page-table
// walk and remains available to unit tests and differential/reference modes.
[[nodiscard]] TranslationResult translate_address(
    CpuBus& bus,
    const CpuSnapshot& state,
    std::uint32_t virtual_address,
    MemoryAccessType access);

// A small local-hart Sv32 TLB. Entries retain leaf permissions so SUM, MXR,
// effective privilege, and store dirty-bit requirements are checked on every
// hit. Both 4 KiB pages and 4 MiB megapages are supported.
class Sv32Tlb {
  public:
    static constexpr std::size_t entry_count = 64U;
    static constexpr std::size_t ways = 4U;
    static constexpr std::size_t set_count = entry_count / ways;

    [[nodiscard]] TranslationResult translate(
        CpuBus& bus,
        const CpuSnapshot& state,
        std::uint32_t virtual_address,
        MemoryAccessType access,
        MmuPerformanceCounters* counters = nullptr);

    void clear() noexcept;

    // Implements the four architectural SFENCE.VMA scopes. A missing virtual
    // address represents rs1=x0; a missing ASID represents rs2=x0.
    void sfence_vma(
        std::optional<std::uint32_t> virtual_address,
        std::optional<std::uint16_t> asid) noexcept;

    [[nodiscard]] std::size_t valid_entries() const noexcept;

  private:
    struct Entry {
        bool valid{};
        bool global{};
        std::uint8_t page_shift{};
        std::uint16_t asid{};
        std::uint32_t root_ppn{};
        std::uint32_t virtual_page{};
        PhysAddr physical_page{};
        std::uint32_t pte{};
    };

    [[nodiscard]] const Entry* lookup(
        const CpuSnapshot& state,
        std::uint32_t virtual_address,
        MemoryAccessType access) const noexcept;

    void insert(
        const CpuSnapshot& state,
        std::uint32_t virtual_address,
        PhysAddr physical_address,
        std::uint32_t pte,
        std::uint8_t page_shift,
        bool global) noexcept;

    std::array<Entry, entry_count> entries_{};
    std::array<std::uint8_t, set_count> replacement_ways_{};
};

} // namespace rv32
