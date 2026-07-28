#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "rv32/core/bus.hpp"
#include "rv32/core/decode.hpp"
#include "rv32/core/mmu.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

enum class FrontendStatus : std::uint8_t {
    Ready,
    InstructionAddressMisaligned,
    InstructionAccessFault,
    InstructionPageFault,
    IllegalInstruction,
};

struct FrontendResult {
    FrontendStatus status{FrontendStatus::InstructionAccessFault};
    std::uint32_t pc{};
    std::uint32_t instruction{};
    DecodedInstruction decoded{};
    BusFault bus_fault{BusFault::None};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == FrontendStatus::Ready;
    }
};

struct DecodePerformanceCounters {
    std::uint64_t lookups{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t invalidations{};
};

struct InstructionCachePerformanceCounters {
    std::uint64_t lookups{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t invalidations{};
};

struct CachedInstruction {
    std::uint32_t instruction{};
    DecodedInstruction decoded{};
};

// A virtual-PC instruction cache scoped by SATP and privilege. Entries are
// inserted only after translation reaches a bus region that explicitly reports
// instruction-cacheable memory. FENCE.I and SFENCE.VMA invalidate the cache.
class InstructionCache {
  public:
    static constexpr std::size_t entry_count = 4096U;

    [[nodiscard]] std::optional<CachedInstruction> lookup(
        std::uint32_t virtual_address,
        const CpuSnapshot* state,
        InstructionCachePerformanceCounters* counters = nullptr)
        const noexcept;

    void insert(
        std::uint32_t virtual_address,
        const CpuSnapshot* state,
        const CachedInstruction& instruction) noexcept;

    void clear(
        InstructionCachePerformanceCounters* counters = nullptr)
        noexcept;

    void sfence_vma(
        std::optional<std::uint32_t> virtual_address,
        std::optional<std::uint16_t> asid,
        InstructionCachePerformanceCounters* counters = nullptr)
        noexcept;

    [[nodiscard]] std::size_t valid_entries() const noexcept;

  private:
    static constexpr std::size_t asid_count = 512U;
    static constexpr std::size_t page_epoch_count = 4096U;

    struct Entry {
        std::uint64_t generation{};
        std::uint32_t virtual_address{};
        std::uint32_t satp{};
        PrivilegeMode privilege{PrivilegeMode::Machine};
        std::uint64_t asid_epoch{};
        std::uint64_t page_epoch{};
        std::uint64_t asid_page_epoch{};
        CachedInstruction instruction{};
    };

    std::array<Entry, entry_count> entries_{};
    std::array<std::uint64_t, asid_count> asid_epochs_{};
    std::array<std::uint64_t, page_epoch_count> page_epochs_{};
    std::array<std::uint64_t, page_epoch_count> asid_page_epochs_{};
    std::uint64_t generation_{1U};
};

// The cache is keyed by the complete raw instruction and its encoded length.
// Instruction bytes are still fetched on every step, so self-modifying code
// cannot reuse a stale decode even before FENCE.I is observed.
class DecodeCache {
  public:
    static constexpr std::size_t entry_count = 1024U;

    [[nodiscard]] DecodedInstruction decode(
        std::uint32_t instruction,
        std::uint8_t length,
        DecodePerformanceCounters* counters = nullptr) noexcept;

    void clear(
        DecodePerformanceCounters* counters = nullptr) noexcept;

    [[nodiscard]] std::size_t valid_entries() const noexcept;

  private:
    struct Entry {
        bool valid{};
        std::uint8_t length{};
        std::uint32_t instruction{};
        DecodedInstruction decoded{};
    };

    std::array<Entry, entry_count> entries_{};
};

[[nodiscard]] FrontendResult fetch_decode(
    CpuBus& bus,
    std::uint32_t pc,
    const CpuSnapshot* state = nullptr,
    Sv32Tlb* tlb = nullptr,
    MmuPerformanceCounters* mmu_counters = nullptr,
    DecodeCache* decode_cache = nullptr,
    DecodePerformanceCounters* decode_counters = nullptr,
    InstructionCache* instruction_cache = nullptr,
    InstructionCachePerformanceCounters*
        instruction_cache_counters = nullptr);

} // namespace rv32
