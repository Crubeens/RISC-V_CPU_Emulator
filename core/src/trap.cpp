#include "rv32/core/trap.hpp"

#include "rv32/core/csr.hpp"

namespace rv32 {

namespace {

[[nodiscard]] constexpr bool valid_privilege(
    PrivilegeMode privilege) noexcept
{
    switch (privilege) {
    case PrivilegeMode::User:
    case PrivilegeMode::Supervisor:
    case PrivilegeMode::Machine:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr PrivilegeMode previous_privilege(
    std::uint32_t mstatus) noexcept
{
    const auto encoded = static_cast<std::uint8_t>(
        (mstatus & mstatus_bits::mpp) >>
        mstatus_bits::mpp_shift);
    return static_cast<PrivilegeMode>(encoded);
}

[[nodiscard]] constexpr PrivilegeMode supervisor_previous_privilege(
    std::uint32_t mstatus) noexcept
{
    return (mstatus & mstatus_bits::spp) != 0U
               ? PrivilegeMode::Supervisor
               : PrivilegeMode::User;
}

[[nodiscard]] constexpr std::uint32_t trap_vector_address(
    std::uint32_t trap_vector,
    bool interrupt,
    std::uint32_t cause) noexcept
{
    const std::uint32_t base = trap_vector & ~0x3U;
    const std::uint32_t mode = trap_vector & 0x3U;
    return interrupt && mode == 1U
               ? base + 4U * cause
               : base;
}

void take_supervisor_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept
{
    CpuSnapshot next = state;
    std::uint32_t mstatus =
        sanitize_mstatus(next.machine_csrs.mstatus);

    const bool interrupts_were_enabled =
        (mstatus & mstatus_bits::sie) != 0U;
    mstatus &= ~(mstatus_bits::sie |
                 mstatus_bits::spie |
                 mstatus_bits::spp);
    if (interrupts_were_enabled) {
        mstatus |= mstatus_bits::spie;
    }
    if (state.privilege == PrivilegeMode::Supervisor) {
        mstatus |= mstatus_bits::spp;
    }

    next.machine_csrs.mstatus = sanitize_mstatus(mstatus);
    next.supervisor_csrs.sepc = request.exception_pc & ~0x3U;
    next.supervisor_csrs.scause =
        static_cast<std::uint32_t>(request.cause);
    next.supervisor_csrs.stval = request.trap_value;
    next.pc = trap_vector_address(
        next.supervisor_csrs.stvec,
        false,
        static_cast<std::uint32_t>(request.cause));
    next.privilege = PrivilegeMode::Supervisor;
    next.waiting_for_interrupt = false;
    next.registers[0] = 0;
    state = next;
}

} // namespace

TrapTarget take_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept
{
    const std::uint32_t cause =
        static_cast<std::uint32_t>(request.cause);
    const bool delegated =
        state.privilege != PrivilegeMode::Machine &&
        cause < 32U &&
        (state.machine_csrs.medeleg & (1U << cause)) != 0U;

    if (delegated) {
        take_supervisor_trap(state, request);
        return TrapTarget::Supervisor;
    }

    take_machine_trap(state, request);
    return TrapTarget::Machine;
}

void take_machine_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept
{
    CpuSnapshot next = state;
    std::uint32_t mstatus =
        sanitize_mstatus(next.machine_csrs.mstatus);

    const bool interrupts_were_enabled =
        (mstatus & mstatus_bits::mie) != 0U;
    mstatus &= ~(mstatus_bits::mie |
                 mstatus_bits::mpie |
                 mstatus_bits::mpp);
    if (interrupts_were_enabled) {
        mstatus |= mstatus_bits::mpie;
    }
    mstatus |=
        static_cast<std::uint32_t>(state.privilege)
        << mstatus_bits::mpp_shift;

    next.machine_csrs.mstatus = sanitize_mstatus(mstatus);
    // This core has IALIGN=32 until the C extension is added.
    next.machine_csrs.mepc = request.exception_pc & ~0x3U;
    next.machine_csrs.mcause =
        static_cast<std::uint32_t>(request.cause);
    next.machine_csrs.mtval = request.trap_value;
    // Synchronous exceptions always enter at BASE in both Direct and
    // Vectored mtvec modes.
    next.pc = trap_vector_address(
        next.machine_csrs.mtvec,
        false,
        static_cast<std::uint32_t>(request.cause));
    next.privilege = PrivilegeMode::Machine;
    next.waiting_for_interrupt = false;
    next.registers[0] = 0;
    state = next;
}

void take_interrupt_trap(
    CpuSnapshot& state,
    const InterruptTrapRequest& request,
    TrapTarget target) noexcept
{
    const std::uint32_t cause =
        static_cast<std::uint32_t>(request.cause);
    CpuSnapshot next = state;
    std::uint32_t mstatus =
        sanitize_mstatus(next.machine_csrs.mstatus);

    if (target == TrapTarget::Supervisor) {
        const bool interrupts_were_enabled =
            (mstatus & mstatus_bits::sie) != 0U;
        mstatus &= ~(mstatus_bits::sie |
                     mstatus_bits::spie |
                     mstatus_bits::spp);
        if (interrupts_were_enabled) {
            mstatus |= mstatus_bits::spie;
        }
        if (state.privilege == PrivilegeMode::Supervisor) {
            mstatus |= mstatus_bits::spp;
        }

        next.machine_csrs.mstatus = sanitize_mstatus(mstatus);
        next.supervisor_csrs.sepc =
            request.interrupted_pc & ~0x3U;
        next.supervisor_csrs.scause = 0x80000000U | cause;
        next.supervisor_csrs.stval = 0;
        next.pc = trap_vector_address(
            next.supervisor_csrs.stvec,
            true,
            cause);
        next.privilege = PrivilegeMode::Supervisor;
    } else {
        const bool interrupts_were_enabled =
            (mstatus & mstatus_bits::mie) != 0U;
        mstatus &= ~(mstatus_bits::mie |
                     mstatus_bits::mpie |
                     mstatus_bits::mpp);
        if (interrupts_were_enabled) {
            mstatus |= mstatus_bits::mpie;
        }
        mstatus |=
            static_cast<std::uint32_t>(state.privilege)
            << mstatus_bits::mpp_shift;

        next.machine_csrs.mstatus = sanitize_mstatus(mstatus);
        next.machine_csrs.mepc =
            request.interrupted_pc & ~0x3U;
        next.machine_csrs.mcause = 0x80000000U | cause;
        next.machine_csrs.mtval = 0;
        next.pc = trap_vector_address(
            next.machine_csrs.mtvec,
            true,
            cause);
        next.privilege = PrivilegeMode::Machine;
    }

    next.waiting_for_interrupt = false;
    next.registers[0] = 0;
    state = next;
}

ExceptionCause environment_call_cause(
    PrivilegeMode privilege) noexcept
{
    switch (privilege) {
    case PrivilegeMode::User:
        return ExceptionCause::EnvironmentCallFromUser;
    case PrivilegeMode::Supervisor:
        return ExceptionCause::EnvironmentCallFromSupervisor;
    case PrivilegeMode::Machine:
        return ExceptionCause::EnvironmentCallFromMachine;
    }
    return ExceptionCause::IllegalInstruction;
}

PrivilegedExecutionResult execute_privileged(
    const DecodedInstruction& decoded,
    const CpuSnapshot& state) noexcept
{
    const PendingCommit not_ready{
        .status = ExecuteStatus::UnsupportedInstruction,
        .pc = state.pc,
        .instruction = decoded.raw,
        .next_pc = state.pc,
    };

    if (decoded.kind != InstructionKind::Mret &&
        decoded.kind != InstructionKind::Sret &&
        decoded.kind != InstructionKind::Wfi) {
        return {
            .status =
                PrivilegedExecutionStatus::NotPrivilegedInstruction,
            .pending = not_ready,
            .trap_value = 0,
        };
    }

    const std::uint32_t old_mstatus =
        sanitize_mstatus(state.machine_csrs.mstatus);
    if (decoded.kind == InstructionKind::Wfi) {
        // This implementation provides WFI to M/S modes. U-mode WFI is the
        // optional form and is intentionally not implemented.
        if (state.privilege == PrivilegeMode::User ||
            (state.privilege == PrivilegeMode::Supervisor &&
             (old_mstatus & mstatus_bits::tw) != 0U)) {
            return {
                .status =
                    PrivilegedExecutionStatus::IllegalInstruction,
                .pending = not_ready,
                .trap_value = decoded.raw,
            };
        }

        return {
            .status = PrivilegedExecutionStatus::Ready,
            .pending = {
                .status = ExecuteStatus::Ready,
                .pc = state.pc,
                .instruction = decoded.raw,
                .next_pc = state.pc + 4U,
                .register_write = {},
                .csr_write = {},
                .privilege_write = {},
                .wait_for_interrupt = true,
            },
            .trap_value = 0,
        };
    }

    if (decoded.kind == InstructionKind::Mret) {
        if (state.privilege != PrivilegeMode::Machine) {
            return {
                .status =
                    PrivilegedExecutionStatus::IllegalInstruction,
                .pending = not_ready,
                .trap_value = decoded.raw,
            };
        }

        const PrivilegeMode return_privilege =
            previous_privilege(old_mstatus);
        if (!valid_privilege(return_privilege)) {
            return {
                .status =
                    PrivilegedExecutionStatus::IllegalInstruction,
                .pending = not_ready,
                .trap_value = decoded.raw,
            };
        }

        std::uint32_t new_mstatus =
            old_mstatus &
            ~(mstatus_bits::mie |
              mstatus_bits::mpie |
              mstatus_bits::mpp);
        if ((old_mstatus & mstatus_bits::mpie) != 0U) {
            new_mstatus |= mstatus_bits::mie;
        }
        new_mstatus |= mstatus_bits::mpie;

        return {
            .status = PrivilegedExecutionStatus::Ready,
            .pending = {
                .status = ExecuteStatus::Ready,
                .pc = state.pc,
                .instruction = decoded.raw,
                .next_pc = state.machine_csrs.mepc,
                .register_write = {},
                .csr_write = {
                    .enabled = true,
                    .address = csr_address::mstatus,
                    .value = new_mstatus,
                },
                .privilege_write = {
                    .enabled = true,
                    .value = return_privilege,
                },
            },
            .trap_value = 0,
        };
    }

    if (state.privilege == PrivilegeMode::User ||
        (state.privilege == PrivilegeMode::Supervisor &&
         (old_mstatus & mstatus_bits::tsr) != 0U)) {
        return {
            .status = PrivilegedExecutionStatus::IllegalInstruction,
            .pending = not_ready,
            .trap_value = decoded.raw,
        };
    }

    const PrivilegeMode return_privilege =
        supervisor_previous_privilege(old_mstatus);
    std::uint32_t new_mstatus =
        old_mstatus &
        ~(mstatus_bits::sie |
          mstatus_bits::spie |
          mstatus_bits::spp);
    if ((old_mstatus & mstatus_bits::spie) != 0U) {
        new_mstatus |= mstatus_bits::sie;
    }
    new_mstatus |= mstatus_bits::spie;

    return {
        .status = PrivilegedExecutionStatus::Ready,
        .pending = {
            .status = ExecuteStatus::Ready,
            .pc = state.pc,
            .instruction = decoded.raw,
            .next_pc = state.supervisor_csrs.sepc,
            .register_write = {},
            .csr_write = {
                .enabled = true,
                .address = csr_address::sstatus,
                .value = new_mstatus,
            },
            .privilege_write = {
                .enabled = true,
                .value = return_privilege,
            },
        },
        .trap_value = 0,
    };
}

} // namespace rv32
