#include "rv32/core/interrupt.hpp"

#include <array>

#include "rv32/core/csr.hpp"

namespace rv32 {

namespace {

struct PriorityEntry {
    InterruptCause cause;
    std::uint32_t bit;
};

// Base privileged ISA priority: MEI, MSI, MTI, SEI, SSI, STI.
inline constexpr std::array priority_order{
    PriorityEntry{
        InterruptCause::MachineExternal,
        interrupt_bits::machine_external,
    },
    PriorityEntry{
        InterruptCause::MachineSoftware,
        interrupt_bits::machine_software,
    },
    PriorityEntry{
        InterruptCause::MachineTimer,
        interrupt_bits::machine_timer,
    },
    PriorityEntry{
        InterruptCause::SupervisorExternal,
        interrupt_bits::supervisor_external,
    },
    PriorityEntry{
        InterruptCause::SupervisorSoftware,
        interrupt_bits::supervisor_software,
    },
    PriorityEntry{
        InterruptCause::SupervisorTimer,
        interrupt_bits::supervisor_timer,
    },
};

[[nodiscard]] bool machine_interrupts_globally_enabled(
    const CpuSnapshot& state) noexcept
{
    return state.privilege != PrivilegeMode::Machine ||
           (state.machine_csrs.mstatus & mstatus_bits::mie) != 0U;
}

[[nodiscard]] bool supervisor_interrupts_globally_enabled(
    const CpuSnapshot& state) noexcept
{
    if (state.privilege == PrivilegeMode::Machine) {
        return false;
    }
    return state.privilege == PrivilegeMode::User ||
           (state.machine_csrs.mstatus & mstatus_bits::sie) != 0U;
}

} // namespace

void sample_interrupt_lines(
    CpuSnapshot& state,
    const IrqLines& lines) noexcept
{
    std::uint32_t pending = 0;
    if (lines.machine_software) {
        pending |= interrupt_bits::machine_software;
    }
    if (lines.machine_timer) {
        pending |= interrupt_bits::machine_timer;
    }
    if (lines.machine_external) {
        pending |= interrupt_bits::machine_external;
    }
    if (lines.supervisor_software) {
        pending |= interrupt_bits::supervisor_software;
    }
    if (lines.supervisor_timer) {
        pending |= interrupt_bits::supervisor_timer;
    }
    if (lines.supervisor_external) {
        pending |= interrupt_bits::supervisor_external;
    }
    state.machine_csrs.mip_lines =
        pending & interrupt_bits::supported;
}

std::uint32_t pending_interrupts(
    const CpuSnapshot& state) noexcept
{
    return (state.machine_csrs.mip_lines |
            state.machine_csrs.mip_software) &
           interrupt_bits::supported;
}

bool interrupt_wake_requested(
    const CpuSnapshot& state) noexcept
{
    return (pending_interrupts(state) &
            state.machine_csrs.mie) != 0U;
}

InterruptSelection select_pending_interrupt(
    const CpuSnapshot& state) noexcept
{
    const std::uint32_t enabled =
        pending_interrupts(state) &
        state.machine_csrs.mie &
        interrupt_bits::supported;

    for (const auto& entry : priority_order) {
        if ((enabled & entry.bit) == 0U) {
            continue;
        }

        const bool delegated =
            (state.machine_csrs.mideleg & entry.bit) != 0U;
        if (delegated) {
            if (supervisor_interrupts_globally_enabled(state)) {
                return {
                    .pending = true,
                    .cause = entry.cause,
                    .target = TrapTarget::Supervisor,
                };
            }
            continue;
        }

        if (machine_interrupts_globally_enabled(state)) {
            return {
                .pending = true,
                .cause = entry.cause,
                .target = TrapTarget::Machine,
            };
        }
    }

    return {};
}

} // namespace rv32
