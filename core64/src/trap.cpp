#include "rv64/core/trap.hpp"

#include "rv64/core/csr.hpp"

namespace rv64 {

namespace {

[[nodiscard]] constexpr bool valid_privilege(
    PrivilegeMode privilege) noexcept
{
    return privilege == PrivilegeMode::User ||
           privilege == PrivilegeMode::Supervisor ||
           privilege == PrivilegeMode::Machine;
}

[[nodiscard]] constexpr Xlen trap_vector_address(
    Xlen tvec,
    bool interrupt,
    Xlen cause) noexcept
{
    const Xlen base = tvec & ~Xlen{3};
    return interrupt && (tvec & Xlen{3}) == 1U
               ? base + 4U * cause
               : base;
}

void take_machine_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept
{
    Xlen status = sanitize_mstatus(state.machine_csrs.mstatus);
    const bool enabled = (status & mstatus_bits::mie) != 0U;
    status &= ~(mstatus_bits::mie |
                mstatus_bits::mpie |
                mstatus_bits::mpp);
    if (enabled) {
        status |= mstatus_bits::mpie;
    }
    status |= static_cast<Xlen>(state.privilege)
              << mstatus_bits::mpp_shift;
    state.machine_csrs.mstatus = sanitize_mstatus(status);
    state.machine_csrs.mepc = request.exception_pc & ~Xlen{1};
    state.machine_csrs.mcause = static_cast<Xlen>(request.cause);
    state.machine_csrs.mtval = request.trap_value;
    state.pc = trap_vector_address(
        state.machine_csrs.mtvec,
        false,
        static_cast<Xlen>(request.cause));
    state.privilege = PrivilegeMode::Machine;
    state.waiting_for_interrupt = false;
    state.registers[0] = 0;
}

void take_supervisor_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept
{
    Xlen status = sanitize_mstatus(state.machine_csrs.mstatus);
    const bool enabled = (status & mstatus_bits::sie) != 0U;
    status &= ~(mstatus_bits::sie |
                mstatus_bits::spie |
                mstatus_bits::spp);
    if (enabled) {
        status |= mstatus_bits::spie;
    }
    if (state.privilege == PrivilegeMode::Supervisor) {
        status |= mstatus_bits::spp;
    }
    state.machine_csrs.mstatus = sanitize_mstatus(status);
    state.supervisor_csrs.sepc = request.exception_pc & ~Xlen{1};
    state.supervisor_csrs.scause = static_cast<Xlen>(request.cause);
    state.supervisor_csrs.stval = request.trap_value;
    state.pc = trap_vector_address(
        state.supervisor_csrs.stvec,
        false,
        static_cast<Xlen>(request.cause));
    state.privilege = PrivilegeMode::Supervisor;
    state.waiting_for_interrupt = false;
    state.registers[0] = 0;
}

} // namespace

TrapTarget take_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept
{
    const Xlen cause = static_cast<Xlen>(request.cause);
    const bool delegated =
        state.privilege != PrivilegeMode::Machine &&
        cause < 64U &&
        (state.machine_csrs.medeleg & (Xlen{1} << cause)) != 0U;
    if (delegated) {
        take_supervisor_trap(state, request);
        return TrapTarget::Supervisor;
    }
    take_machine_trap(state, request);
    return TrapTarget::Machine;
}

void take_interrupt_trap(
    CpuSnapshot& state,
    const InterruptTrapRequest& request,
    TrapTarget target) noexcept
{
    const Xlen cause = static_cast<Xlen>(request.cause);
    Xlen status = sanitize_mstatus(state.machine_csrs.mstatus);
    if (target == TrapTarget::Supervisor) {
        const bool enabled = (status & mstatus_bits::sie) != 0U;
        status &= ~(mstatus_bits::sie |
                    mstatus_bits::spie |
                    mstatus_bits::spp);
        if (enabled) {
            status |= mstatus_bits::spie;
        }
        if (state.privilege == PrivilegeMode::Supervisor) {
            status |= mstatus_bits::spp;
        }
        state.machine_csrs.mstatus = sanitize_mstatus(status);
        state.supervisor_csrs.sepc =
            request.interrupted_pc & ~Xlen{1};
        state.supervisor_csrs.scause =
            (Xlen{1} << 63U) | cause;
        state.supervisor_csrs.stval = 0;
        state.pc = trap_vector_address(
            state.supervisor_csrs.stvec,
            true,
            cause);
        state.privilege = PrivilegeMode::Supervisor;
    } else {
        const bool enabled = (status & mstatus_bits::mie) != 0U;
        status &= ~(mstatus_bits::mie |
                    mstatus_bits::mpie |
                    mstatus_bits::mpp);
        if (enabled) {
            status |= mstatus_bits::mpie;
        }
        status |= static_cast<Xlen>(state.privilege)
                  << mstatus_bits::mpp_shift;
        state.machine_csrs.mstatus = sanitize_mstatus(status);
        state.machine_csrs.mepc =
            request.interrupted_pc & ~Xlen{1};
        state.machine_csrs.mcause =
            (Xlen{1} << 63U) | cause;
        state.machine_csrs.mtval = 0;
        state.pc = trap_vector_address(
            state.machine_csrs.mtvec,
            true,
            cause);
        state.privilege = PrivilegeMode::Machine;
    }
    state.waiting_for_interrupt = false;
    state.registers[0] = 0;
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

bool execute_mret(CpuSnapshot& state) noexcept
{
    if (state.privilege != PrivilegeMode::Machine) {
        return false;
    }
    const Xlen old = sanitize_mstatus(state.machine_csrs.mstatus);
    const auto target = static_cast<PrivilegeMode>(
        (old & mstatus_bits::mpp) >> mstatus_bits::mpp_shift);
    if (!valid_privilege(target)) {
        return false;
    }
    Xlen next = old & ~(mstatus_bits::mie |
                        mstatus_bits::mpie |
                        mstatus_bits::mpp);
    if ((old & mstatus_bits::mpie) != 0U) {
        next |= mstatus_bits::mie;
    }
    next |= mstatus_bits::mpie;
    if (target != PrivilegeMode::Machine) {
        next &= ~mstatus_bits::mprv;
    }
    state.machine_csrs.mstatus = sanitize_mstatus(next);
    state.privilege = target;
    state.pc = state.machine_csrs.mepc;
    return true;
}

bool execute_sret(CpuSnapshot& state) noexcept
{
    const Xlen old = sanitize_mstatus(state.machine_csrs.mstatus);
    if (state.privilege == PrivilegeMode::User ||
        (state.privilege == PrivilegeMode::Supervisor &&
         (old & mstatus_bits::tsr) != 0U)) {
        return false;
    }
    const PrivilegeMode target =
        (old & mstatus_bits::spp) != 0U
            ? PrivilegeMode::Supervisor
            : PrivilegeMode::User;
    Xlen next = old & ~(mstatus_bits::sie |
                        mstatus_bits::spie |
                        mstatus_bits::spp);
    if ((old & mstatus_bits::spie) != 0U) {
        next |= mstatus_bits::sie;
    }
    next |= mstatus_bits::spie;
    next &= ~mstatus_bits::mprv;
    state.machine_csrs.mstatus = sanitize_mstatus(next);
    state.privilege = target;
    state.pc = state.supervisor_csrs.sepc;
    return true;
}

bool wfi_allowed(const CpuSnapshot& state) noexcept
{
    return state.privilege != PrivilegeMode::User &&
           !(state.privilege == PrivilegeMode::Supervisor &&
             (state.machine_csrs.mstatus & mstatus_bits::tw) != 0U);
}

bool sfence_vma_allowed(const CpuSnapshot& state) noexcept
{
    return state.privilege != PrivilegeMode::User &&
           !(state.privilege == PrivilegeMode::Supervisor &&
             (state.machine_csrs.mstatus & mstatus_bits::tvm) != 0U);
}

} // namespace rv64
