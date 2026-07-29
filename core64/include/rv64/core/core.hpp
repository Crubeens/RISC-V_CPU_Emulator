#pragma once

#include "rv/common/bus.hpp"
#include "rv64/core/mmu.hpp"
#include "rv64/core/types.hpp"

namespace rv64 {

class Core {
  public:
    explicit Core(rv::CpuBus& bus) noexcept;

    void reset(const ResetConfig& config = {}) noexcept;
    [[nodiscard]] StepResult step(const IrqLines& irq_lines = {});
    [[nodiscard]] const CpuSnapshot& snapshot() const noexcept;
    [[nodiscard]] const IrqLines& sampled_irq_lines() const noexcept;
    [[nodiscard]] std::size_t tlb_entries() const noexcept;

  private:
    rv::CpuBus* bus_{};
    CpuSnapshot state_{};
    IrqLines sampled_irq_lines_{};
    Sv39Tlb tlb_{};
};

} // namespace rv64
