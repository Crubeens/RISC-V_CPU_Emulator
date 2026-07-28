#include "rv32/core/core.hpp"

#include <algorithm>

#include "rv32/core/csr.hpp"
#include "rv32/core/execute.hpp"
#include "rv32/core/frontend.hpp"
#include "rv32/core/interrupt.hpp"
#include "rv32/core/trap.hpp"

namespace rv32 {

Core::Core(CpuBus& bus) noexcept : bus_(&bus)
{
    reset();
}

void Core::reset(const ResetConfig& config)
{
    state_ = {};
    state_.pc = config.reset_pc;
    state_.hart_id = config.hart_id;
    state_.privilege = config.initial_privilege;
    state_.registers[10] = config.hart_id;
    state_.registers[11] = config.boot_argument;
    state_.registers[0] = 0;
    sampled_irq_lines_ = {};
}

StepResult Core::step(const IrqLines& irq_lines)
{
    sampled_irq_lines_ = irq_lines;
    state_.registers[0] = 0;
    sample_interrupt_lines(state_, irq_lines);

    if (state_.waiting_for_interrupt) {
        if (!interrupt_wake_requested(state_)) {
            return {
                .status = StepStatus::WaitingForInterrupt,
                .pc = state_.pc,
                .instruction = 0,
                .trap_value = 0,
                .bus_fault = BusFault::None,
                .cycle = state_.cycle,
            };
        }
        state_.waiting_for_interrupt = false;
    }

    const InterruptSelection interrupt =
        select_pending_interrupt(state_);
    if (interrupt.pending) {
        const std::uint32_t interrupted_pc = state_.pc;
        take_interrupt_trap(
            state_,
            {
                .cause = interrupt.cause,
                .interrupted_pc = interrupted_pc,
            },
            interrupt.target);
        return {
            .status = StepStatus::TrapTaken,
            .pc = interrupted_pc,
            .instruction = 0,
            .trap_value = 0,
            .bus_fault = BusFault::None,
            .cycle = state_.cycle,
        };
    }

    CsrFile csr_file(state_, *bus_);
    const PrivilegeMode executing_privilege = state_.privilege;

    const FrontendResult frontend_result =
        fetch_decode(*bus_, state_.pc, &state_);
    const auto trap =
        [&](ExceptionCause cause,
            std::uint32_t trap_value,
            BusFault bus_fault) -> StepResult {
        static_cast<void>(take_trap(
            state_,
            {
                .cause = cause,
                .exception_pc = frontend_result.pc,
                .trap_value = trap_value,
            }));
        return {
            .status = StepStatus::TrapTaken,
            .pc = frontend_result.pc,
            .instruction = frontend_result.instruction,
            .trap_value = trap_value,
            .bus_fault = bus_fault,
            .cycle = state_.cycle,
        };
    };

    switch (frontend_result.status) {
    case FrontendStatus::IllegalInstruction:
        return trap(
            ExceptionCause::IllegalInstruction,
            frontend_result.trap_value,
            frontend_result.bus_fault);
    case FrontendStatus::InstructionAccessFault:
        return trap(
            ExceptionCause::InstructionAccessFault,
            frontend_result.trap_value,
            frontend_result.bus_fault);
    case FrontendStatus::InstructionPageFault:
        return trap(
            ExceptionCause::InstructionPageFault,
            frontend_result.trap_value,
            frontend_result.bus_fault);
    case FrontendStatus::InstructionAddressMisaligned:
        return trap(
            ExceptionCause::InstructionAddressMisaligned,
            frontend_result.trap_value,
            frontend_result.bus_fault);
    case FrontendStatus::Ready:
        break;
    }

    const std::uint32_t rs1_value =
        state_.registers[frontend_result.decoded.rs1];
    const std::uint32_t rs2_value =
        state_.registers[frontend_result.decoded.rs2];
    PendingCommit pending = execute_decoded(
        frontend_result.decoded,
        frontend_result.pc,
        rs1_value,
        rs2_value);

    switch (pending.status) {
    case ExecuteStatus::EnvironmentCall:
        return trap(
            environment_call_cause(state_.privilege),
            0,
            BusFault::None);
    case ExecuteStatus::Breakpoint:
        return trap(
            ExceptionCause::Breakpoint,
            frontend_result.pc,
            BusFault::None);
    case ExecuteStatus::Ready:
    case ExecuteStatus::UnsupportedInstruction:
        break;
    }

    if (!pending.ready()) {
        const PrivilegedExecutionResult privileged_result =
            execute_privileged(
                frontend_result.decoded,
                state_);
        switch (privileged_result.status) {
        case PrivilegedExecutionStatus::Ready:
            pending = privileged_result.pending;
            break;
        case PrivilegedExecutionStatus::IllegalInstruction:
            return trap(
                ExceptionCause::IllegalInstruction,
                privileged_result.trap_value,
                BusFault::None);
        case PrivilegedExecutionStatus::NotPrivilegedInstruction:
            break;
        }
    }

    if (!pending.ready()) {
        const ControlFlowResult control_flow_result =
            execute_control_flow(
                frontend_result.decoded,
                frontend_result.pc,
                rs1_value,
                rs2_value);

        switch (control_flow_result.status) {
        case ControlFlowStatus::Ready:
            pending = control_flow_result.pending;
            break;
        case ControlFlowStatus::InstructionAddressMisaligned:
            return trap(
                ExceptionCause::InstructionAddressMisaligned,
                control_flow_result.trap_value,
                BusFault::None);
        case ControlFlowStatus::NotControlFlowInstruction:
            break;
        }
    }

    if (!pending.ready()) {
        const AtomicExecutionResult atomic_result =
            execute_atomic(
                *bus_,
                frontend_result.decoded,
                frontend_result.pc,
                state_.hart_id,
                rs1_value,
                rs2_value,
                &state_);

        switch (atomic_result.status) {
        case AtomicStatus::Ready:
            pending = atomic_result.pending;
            break;
        case AtomicStatus::LoadAddressMisaligned:
            return trap(
                ExceptionCause::LoadAddressMisaligned,
                atomic_result.trap_value,
                atomic_result.bus_fault);
        case AtomicStatus::LoadAccessFault:
            return trap(
                ExceptionCause::LoadAccessFault,
                atomic_result.trap_value,
                atomic_result.bus_fault);
        case AtomicStatus::LoadPageFault:
            return trap(
                ExceptionCause::LoadPageFault,
                atomic_result.trap_value,
                atomic_result.bus_fault);
        case AtomicStatus::StoreAddressMisaligned:
            return trap(
                ExceptionCause::StoreAddressMisaligned,
                atomic_result.trap_value,
                atomic_result.bus_fault);
        case AtomicStatus::StoreAccessFault:
            return trap(
                ExceptionCause::StoreAccessFault,
                atomic_result.trap_value,
                atomic_result.bus_fault);
        case AtomicStatus::StorePageFault:
            return trap(
                ExceptionCause::StorePageFault,
                atomic_result.trap_value,
                atomic_result.bus_fault);
        case AtomicStatus::NotAtomicInstruction:
            break;
        }
    }

    if (!pending.ready()) {
        const CsrExecutionResult csr_result = execute_csr(
            csr_file,
            frontend_result.decoded,
            state_.privilege,
            frontend_result.pc,
            rs1_value);

        switch (csr_result.status) {
        case CsrExecutionStatus::Ready:
            pending = csr_result.pending;
            break;
        case CsrExecutionStatus::IllegalInstruction:
            return trap(
                ExceptionCause::IllegalInstruction,
                csr_result.trap_value,
                BusFault::None);
        case CsrExecutionStatus::NotCsrInstruction:
            break;
        }
    }

    if (!pending.ready()) {
        const MemoryResult memory_result = execute_memory(
            *bus_,
            frontend_result.decoded,
            frontend_result.pc,
            rs1_value,
            rs2_value,
            &state_);

        switch (memory_result.status) {
        case MemoryStatus::Ready:
            pending = memory_result.pending;
            break;
        case MemoryStatus::LoadAddressMisaligned:
            return trap(
                ExceptionCause::LoadAddressMisaligned,
                memory_result.trap_value,
                memory_result.bus_fault);
        case MemoryStatus::LoadAccessFault:
            return trap(
                ExceptionCause::LoadAccessFault,
                memory_result.trap_value,
                memory_result.bus_fault);
        case MemoryStatus::LoadPageFault:
            return trap(
                ExceptionCause::LoadPageFault,
                memory_result.trap_value,
                memory_result.bus_fault);
        case MemoryStatus::StoreAddressMisaligned:
            return trap(
                ExceptionCause::StoreAddressMisaligned,
                memory_result.trap_value,
                memory_result.bus_fault);
        case MemoryStatus::StoreAccessFault:
            return trap(
                ExceptionCause::StoreAccessFault,
                memory_result.trap_value,
                memory_result.bus_fault);
        case MemoryStatus::StorePageFault:
            return trap(
                ExceptionCause::StorePageFault,
                memory_result.trap_value,
                memory_result.bus_fault);
        case MemoryStatus::NotMemoryInstruction:
            return trap(
                ExceptionCause::IllegalInstruction,
                frontend_result.instruction,
                BusFault::None);
        }
    }

    if (!commit_pending(state_, pending, &csr_file)) {
        return {
            .status = StepStatus::CoreNotImplemented,
            .pc = frontend_result.pc,
            .instruction = frontend_result.instruction,
            .trap_value = frontend_result.instruction,
            .bus_fault = BusFault::None,
            .cycle = state_.cycle,
        };
    }

    return {
        .status = state_.waiting_for_interrupt
                      ? StepStatus::WaitingForInterrupt
                      : StepStatus::Retired,
        .pc = frontend_result.pc,
        .instruction = frontend_result.instruction,
        .trap_value = 0,
        .bus_fault = BusFault::None,
        .cycle = state_.cycle,
        .commit = {
            .valid = true,
            .privilege = executing_privilege,
            .pc = pending.pc,
            .instruction = pending.instruction,
            .next_pc = state_.pc,
            .instruction_length =
                frontend_result.decoded.length,
            .register_write = {
                .enabled =
                    pending.register_write.enabled &&
                    pending.register_write.index != 0U,
                .index = pending.register_write.index,
                .value = pending.register_write.value,
            },
        },
    };
}

void Core::advance_cycles(std::uint64_t cycles) noexcept
{
    state_.cycle += cycles;
}

CpuSnapshot Core::snapshot() const noexcept
{
    return state_;
}

const IrqLines& Core::sampled_irq_lines() const noexcept
{
    return sampled_irq_lines_;
}

} // namespace rv32
