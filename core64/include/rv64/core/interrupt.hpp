#pragma once

#include "rv64/core/trap.hpp"
#include "rv64/core/types.hpp"

namespace rv64 {

namespace interrupt_bits {

inline constexpr Xlen supervisor_software = Xlen{1} << 1U;
inline constexpr Xlen machine_software = Xlen{1} << 3U;
inline constexpr Xlen supervisor_timer = Xlen{1} << 5U;
inline constexpr Xlen machine_timer = Xlen{1} << 7U;
inline constexpr Xlen supervisor_external = Xlen{1} << 9U;
inline constexpr Xlen machine_external = Xlen{1} << 11U;
inline constexpr Xlen supervisor =
    supervisor_software | supervisor_timer | supervisor_external;
inline constexpr Xlen machine =
    machine_software | machine_timer | machine_external;
inline constexpr Xlen supported = supervisor | machine;
inline constexpr Xlen machine_writable_pending = supervisor;
inline constexpr Xlen supervisor_writable_pending =
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
[[nodiscard]] Xlen pending_interrupts(
    const CpuSnapshot& state) noexcept;
[[nodiscard]] bool interrupt_wake_requested(
    const CpuSnapshot& state) noexcept;
[[nodiscard]] InterruptSelection select_pending_interrupt(
    const CpuSnapshot& state) noexcept;

} // namespace rv64
