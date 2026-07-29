#pragma once

#include "rv/common/bus.hpp"
#include "rv64/core/types.hpp"

namespace rv64 {

class Core {
  public:
    explicit Core(rv::CpuBus& bus) noexcept;

    void reset(const ResetConfig& config = {}) noexcept;
    [[nodiscard]] StepResult step();
    [[nodiscard]] const CpuSnapshot& snapshot() const noexcept;

  private:
    rv::CpuBus* bus_{};
    CpuSnapshot state_{};
    std::uint32_t hart_id_{};
};

} // namespace rv64
