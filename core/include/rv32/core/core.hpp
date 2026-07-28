#pragma once

#include <cstdint>
#include <string_view>

#include "rv32/core/bus.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

class Core {
  public:
    explicit Core(CpuBus& bus) noexcept;

    void reset(const ResetConfig& config = {});

    [[nodiscard]] StepResult step(const IrqLines& irq_lines);

    void advance_cycles(std::uint64_t cycles) noexcept;

    [[nodiscard]] CpuSnapshot snapshot() const noexcept;

    [[nodiscard]] const IrqLines& sampled_irq_lines() const noexcept;

    [[nodiscard]] static constexpr std::string_view isa_string() noexcept
    {
        return "rv32imac_zicntr_zicsr_zifencei";
    }

  private:
    CpuBus* bus_;
    CpuSnapshot state_{};
    IrqLines sampled_irq_lines_{};
};

} // namespace rv32
