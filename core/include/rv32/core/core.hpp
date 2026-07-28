#pragma once

#include <cstdint>
#include <string_view>

#include "rv32/core/bus.hpp"
#include "rv32/core/frontend.hpp"
#include "rv32/core/mmu.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

struct CorePerformanceCounters {
    std::uint64_t step_calls{};
    std::uint64_t retired_instructions{};
    std::uint64_t synchronous_traps{};
    std::uint64_t interrupt_traps{};
    std::uint64_t waiting_returns{};
    MmuPerformanceCounters mmu{};
    DecodePerformanceCounters decode{};
    InstructionCachePerformanceCounters instruction_cache{};
};

enum class ExecutionMode : std::uint8_t {
    Reference,
    Fast,
};

class Core {
  public:
    explicit Core(CpuBus& bus) noexcept;

    void reset(const ResetConfig& config = {});

    [[nodiscard]] StepResult step(const IrqLines& irq_lines);

    void advance_cycles(std::uint64_t cycles) noexcept;

    [[nodiscard]] CpuSnapshot snapshot() const noexcept;

    [[nodiscard]] const IrqLines& sampled_irq_lines() const noexcept;

    [[nodiscard]] const CorePerformanceCounters&
    performance_counters() const noexcept;

    [[nodiscard]] std::size_t tlb_entries() const noexcept;

    void set_execution_mode(ExecutionMode mode) noexcept;

    [[nodiscard]] ExecutionMode execution_mode() const noexcept;

    [[nodiscard]] std::size_t decoded_entries() const noexcept;

    [[nodiscard]] std::size_t instruction_cache_entries() const noexcept;

    [[nodiscard]] static constexpr std::string_view isa_string() noexcept
    {
        return "rv32imac_zicntr_zicsr_zifencei";
    }

  private:
    CpuBus* bus_;
    CpuSnapshot state_{};
    IrqLines sampled_irq_lines_{};
    Sv32Tlb tlb_{};
    DecodeCache decode_cache_{};
    InstructionCache instruction_cache_{};
    CorePerformanceCounters performance_counters_{};
    ExecutionMode execution_mode_{ExecutionMode::Fast};
};

} // namespace rv32
