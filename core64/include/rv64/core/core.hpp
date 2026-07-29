#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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

    void set_execution_mode(ExecutionMode mode) noexcept;
    [[nodiscard]] ExecutionMode execution_mode() const noexcept;
    [[nodiscard]] std::size_t decoded_entries() const noexcept;

    [[nodiscard]] static constexpr std::string_view isa_string() noexcept
    {
        return "rv64imac_zicntr_zicsr_zifencei";
    }

  private:
    static constexpr std::size_t decode_cache_entries = 1024U;

    struct DecodeCacheEntry {
        bool valid{};
        std::uint8_t length{};
        std::uint32_t instruction{};
        DecodedInstruction decoded{};
    };

    [[nodiscard]] DecodedInstruction decode(
        std::uint32_t instruction,
        std::uint8_t length) noexcept;
    void clear_decode_cache(bool count_invalidation) noexcept;

    rv::CpuBus* bus_{};
    CpuSnapshot state_{};
    IrqLines sampled_irq_lines_{};
    Sv39Tlb tlb_{};
    std::array<DecodeCacheEntry, decode_cache_entries> decode_cache_{};
    CorePerformanceCounters performance_counters_{};
    ExecutionMode execution_mode_{ExecutionMode::Fast};
};

} // namespace rv64
