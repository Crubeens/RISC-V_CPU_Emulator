#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "rv/common/bus.hpp"
#include "rv64/core/decode.hpp"
#include "rv64/core/mmu.hpp"
#include "rv64/core/types.hpp"

namespace rv64 {

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

struct FetchPerformanceCounters {
    std::uint64_t instruction_fetches{};
    std::uint64_t halfword_reads{};
    std::uint64_t compressed_instructions{};
    std::uint64_t standard_instructions{};
};

struct CorePerformanceCounters {
    std::uint64_t step_calls{};
    std::uint64_t retired_instructions{};
    std::uint64_t synchronous_traps{};
    std::uint64_t interrupt_traps{};
    std::uint64_t waiting_returns{};
    MmuPerformanceCounters mmu{};
    DecodePerformanceCounters decode{};
    InstructionCachePerformanceCounters instruction_cache{};
    FetchPerformanceCounters fetch{};
};

enum class ExecutionMode : std::uint8_t {
    Reference,
    Fast,
};

class Core {
  public:
    explicit Core(rv::CpuBus& bus) noexcept;

    void reset(const ResetConfig& config = {}) noexcept;
    [[nodiscard]] StepResult step(const IrqLines& irq_lines = {});
    [[nodiscard]] const CpuSnapshot& snapshot() const noexcept;
    [[nodiscard]] const IrqLines& sampled_irq_lines() const noexcept;
    [[nodiscard]] const CorePerformanceCounters&
    performance_counters() const noexcept;
    [[nodiscard]] std::size_t tlb_entries() const noexcept;
    [[nodiscard]] std::size_t instruction_cache_entries() const noexcept;

    void set_execution_mode(ExecutionMode mode) noexcept;
    [[nodiscard]] ExecutionMode execution_mode() const noexcept;
    [[nodiscard]] std::size_t decoded_entries() const noexcept;

    [[nodiscard]] static constexpr std::string_view isa_string() noexcept
    {
        return "rv64imafdc_zicntr_zicsr_zifencei";
    }

  private:
    static constexpr std::size_t decode_cache_entries = 1024U;
    static constexpr std::size_t instruction_cache_entry_count = 4096U;
    static constexpr std::size_t instruction_cache_epoch_count = 4096U;

    struct DecodeCacheEntry {
        bool valid{};
        std::uint8_t length{};
        std::uint32_t instruction{};
        DecodedInstruction decoded{};
    };

    struct InstructionCacheEntry {
        std::uint64_t generation{};
        Xlen virtual_address{};
        Xlen satp{};
        PrivilegeMode privilege{PrivilegeMode::Machine};
        std::uint64_t asid_epoch{};
        std::uint64_t page_epoch{};
        std::uint64_t asid_page_epoch{};
        std::uint32_t instruction{};
        DecodedInstruction decoded{};
    };

    [[nodiscard]] DecodedInstruction decode(
        std::uint32_t instruction,
        std::uint8_t length) noexcept;
    void clear_decode_cache(bool count_invalidation) noexcept;
    [[nodiscard]] bool lookup_instruction_cache(
        Xlen virtual_address,
        std::uint32_t& instruction,
        DecodedInstruction& decoded) noexcept;
    void insert_instruction_cache(
        Xlen virtual_address,
        std::uint32_t instruction,
        const DecodedInstruction& decoded) noexcept;
    void clear_instruction_cache(bool count_invalidation) noexcept;
    void sfence_instruction_cache(
        std::optional<Xlen> virtual_address,
        std::optional<std::uint16_t> asid) noexcept;

    rv::CpuBus* bus_{};
    CpuSnapshot state_{};
    IrqLines sampled_irq_lines_{};
    Sv39Tlb tlb_{};
    std::array<DecodeCacheEntry, decode_cache_entries> decode_cache_{};
    std::array<
        InstructionCacheEntry,
        instruction_cache_entry_count>
        instruction_cache_{};
    std::array<std::uint64_t, instruction_cache_epoch_count>
        instruction_cache_asid_epochs_{};
    std::array<std::uint64_t, instruction_cache_epoch_count>
        instruction_cache_page_epochs_{};
    std::array<std::uint64_t, instruction_cache_epoch_count>
        instruction_cache_asid_page_epochs_{};
    std::uint64_t instruction_cache_generation_{1U};
    CorePerformanceCounters performance_counters_{};
    ExecutionMode execution_mode_{ExecutionMode::Fast};
};

} // namespace rv64
