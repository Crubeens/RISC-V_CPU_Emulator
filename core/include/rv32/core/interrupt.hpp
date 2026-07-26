#pragma once

#include <cstdint>

#include "rv32/core/trap.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

namespace interrupt_bits {

inline constexpr std::uint32_t supervisor_software = 1U << 1U;
inline constexpr std::uint32_t machine_software = 1U << 3U;
inline constexpr std::uint32_t supervisor_timer = 1U << 5U;
inline constexpr std::uint32_t machine_timer = 1U << 7U;
inline constexpr std::uint32_t supervisor_external = 1U << 9U;
inline constexpr std::uint32_t machine_external = 1U << 11U;

inline constexpr std::uint32_t supervisor =
    supervisor_software |
    supervisor_timer |
    supervisor_external;
inline constexpr std::uint32_t machine =
    machine_software |
    machine_timer |
    machine_external;
inline constexpr std::uint32_t supported = supervisor | machine;

// M-mode software can inject all three supervisor interrupt classes through
// mip. S-mode itself may directly write only SSIP through sip.
inline constexpr std::uint32_t machine_writable_pending = supervisor;
inline constexpr std::uint32_t supervisor_writable_pending =
    supervisor_software;

} // namespace interrupt_bits

struct InterruptSelection {
    bool pending{};
    InterruptCause cause{InterruptCause::MachineExternal};
    TrapTarget target{TrapTarget::Machine};
};

void sample_interrupt_lines(
    CpuSnapshot& state,
    const IrqLines& lines) noexcept;

[[nodiscard]] std::uint32_t pending_interrupts(
    const CpuSnapshot& state) noexcept;

[[nodiscard]] bool interrupt_wake_requested(
    const CpuSnapshot& state) noexcept;

[[nodiscard]] InterruptSelection select_pending_interrupt(
    const CpuSnapshot& state) noexcept;

} // namespace rv32
