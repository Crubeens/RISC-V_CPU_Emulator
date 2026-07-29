#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "rv/common/bus.hpp"
#include "rv64/core/types.hpp"

namespace rv64 {

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
    rv::PhysAddr physical_address{};
    rv::BusFault bus_fault{rv::BusFault::None};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == TranslationStatus::Ready;
    }
};

namespace satp_bits {

inline constexpr Xlen mode = Xlen{0xF} << 60U;
inline constexpr Xlen sv39_mode = Xlen{8} << 60U;
inline constexpr Xlen asid = Xlen{0xFFFF} << 44U;
inline constexpr Xlen ppn = (Xlen{1} << 44U) - 1U;

} // namespace satp_bits

namespace sv39_pte_bits {

inline constexpr Xlen valid = Xlen{1} << 0U;
inline constexpr Xlen read = Xlen{1} << 1U;
inline constexpr Xlen write = Xlen{1} << 2U;
inline constexpr Xlen execute = Xlen{1} << 3U;
inline constexpr Xlen user = Xlen{1} << 4U;
inline constexpr Xlen global = Xlen{1} << 5U;
inline constexpr Xlen accessed = Xlen{1} << 6U;
inline constexpr Xlen dirty = Xlen{1} << 7U;
inline constexpr Xlen rsw = Xlen{3} << 8U;
inline constexpr Xlen ppn = ((Xlen{1} << 44U) - 1U) << 10U;
inline constexpr Xlen reserved = Xlen{0x3FF} << 54U;

} // namespace sv39_pte_bits

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

// Bare writes are normalized to zero. Sv39 writes retain MODE, the complete
// sixteen-bit ASID, and the forty-four-bit root page number.
[[nodiscard]] Xlen sanitize_satp(Xlen value) noexcept;

[[nodiscard]] PrivilegeMode effective_privilege(
    const CpuSnapshot& state,
    MemoryAccessType access) noexcept;

[[nodiscard]] TranslationResult translate_address(
    rv::CpuBus& bus,
    const CpuSnapshot& state,
    Xlen virtual_address,
    MemoryAccessType access);

// A local-hart, 64-entry, four-way Sv39 TLB. Each entry keeps the leaf PTE so
// privilege, SUM, MXR, and access-type checks remain effective on every hit.
class Sv39Tlb {
  public:
    static constexpr std::size_t entry_count = 64U;
    static constexpr std::size_t ways = 4U;
    static constexpr std::size_t set_count = entry_count / ways;

    [[nodiscard]] TranslationResult translate(
        rv::CpuBus& bus,
        const CpuSnapshot& state,
        Xlen virtual_address,
        MemoryAccessType access,
        MmuPerformanceCounters* counters = nullptr);

    void clear() noexcept;

    // Missing values represent rs1=x0 and rs2=x0 respectively.
    void sfence_vma(
        std::optional<Xlen> virtual_address,
        std::optional<std::uint16_t> asid) noexcept;

    [[nodiscard]] std::size_t valid_entries() const noexcept;

  private:
    struct Entry {
        bool valid{};
        bool global{};
        std::uint8_t page_shift{};
        std::uint16_t asid{};
        Xlen root_ppn{};
        Xlen virtual_page{};
        rv::PhysAddr physical_page{};
        Xlen pte{};
    };

    [[nodiscard]] const Entry* lookup(
        const CpuSnapshot& state,
        Xlen virtual_address,
        MemoryAccessType access) const noexcept;

    void insert(
        const CpuSnapshot& state,
        Xlen virtual_address,
        rv::PhysAddr physical_address,
        Xlen pte,
        std::uint8_t page_shift,
        bool global) noexcept;

    std::array<Entry, entry_count> entries_{};
    std::array<std::uint8_t, set_count> replacement_ways_{};
};

} // namespace rv64
