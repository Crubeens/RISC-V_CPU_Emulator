#include "rv32/core/core.hpp"

#include <algorithm>
#include <optional>

#include "rv32/core/csr.hpp"
#include "rv32/core/execute.hpp"
#include "rv32/core/frontend.hpp"
#include "rv32/core/interrupt.hpp"
#include "rv32/core/trap.hpp"

namespace rv32 {

namespace {

enum class ExecutionUnit : std::uint8_t {
    Integer,
    Privileged,
    ControlFlow,
    Atomic,
    Csr,
    Memory,
};

[[nodiscard]] constexpr ExecutionUnit execution_unit(
    InstructionKind kind) noexcept
{
    switch (kind) {
    case InstructionKind::Mret:
    case InstructionKind::Sret:
    case InstructionKind::Wfi:
    case InstructionKind::SfenceVma:
        return ExecutionUnit::Privileged;
    case InstructionKind::Auipc:
    case InstructionKind::Jal:
    case InstructionKind::Jalr:
    case InstructionKind::Beq:
    case InstructionKind::Bne:
    case InstructionKind::Blt:
    case InstructionKind::Bge:
    case InstructionKind::Bltu:
    case InstructionKind::Bgeu:
        return ExecutionUnit::ControlFlow;
    case InstructionKind::LrW:
    case InstructionKind::ScW:
    case InstructionKind::AmoSwapW:
    case InstructionKind::AmoAddW:
    case InstructionKind::AmoXorW:
    case InstructionKind::AmoAndW:
    case InstructionKind::AmoOrW:
    case InstructionKind::AmoMinW:
    case InstructionKind::AmoMaxW:
    case InstructionKind::AmoMinuW:
    case InstructionKind::AmoMaxuW:
        return ExecutionUnit::Atomic;
    case InstructionKind::Csrrw:
    case InstructionKind::Csrrs:
    case InstructionKind::Csrrc:
    case InstructionKind::Csrrwi:
    case InstructionKind::Csrrsi:
    case InstructionKind::Csrrci:
        return ExecutionUnit::Csr;
    case InstructionKind::Lb:
    case InstructionKind::Lh:
    case InstructionKind::Lw:
    case InstructionKind::Lbu:
    case InstructionKind::Lhu:
    case InstructionKind::Sb:
    case InstructionKind::Sh:
    case InstructionKind::Sw:
        return ExecutionUnit::Memory;
    default:
        return ExecutionUnit::Integer;
    }
}

} // namespace

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
    tlb_.clear();
    decode_cache_.clear();
    instruction_cache_.clear();
    performance_counters_ = {};
}

StepResult Core::step(const IrqLines& irq_lines)
{
    ++performance_counters_.step_calls;
    sampled_irq_lines_ = irq_lines;
    state_.registers[0] = 0;
    sample_interrupt_lines(state_, irq_lines);

    if (state_.waiting_for_interrupt) {
        if (!interrupt_wake_requested(state_)) {
            ++performance_counters_.waiting_returns;
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
        ++performance_counters_.interrupt_traps;
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
        fetch_decode(
            *bus_,
            state_.pc,
            &state_,
            execution_mode_ == ExecutionMode::Fast
                ? &tlb_
                : nullptr,
            execution_mode_ == ExecutionMode::Fast
                ? &performance_counters_.mmu
                : nullptr,
            execution_mode_ == ExecutionMode::Fast
                ? &decode_cache_
                : nullptr,
            execution_mode_ == ExecutionMode::Fast
                ? &performance_counters_.decode
                : nullptr,
            execution_mode_ == ExecutionMode::Fast
                ? &instruction_cache_
                : nullptr,
            execution_mode_ == ExecutionMode::Fast
                ? &performance_counters_.instruction_cache
                : nullptr);
    const auto trap =
        [&](ExceptionCause cause,
            std::uint32_t trap_value,
            BusFault bus_fault) -> StepResult {
        ++performance_counters_.synchronous_traps;
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
    const ExecutionUnit unit =
        execution_unit(frontend_result.decoded.kind);
    const bool reference =
        execution_mode_ == ExecutionMode::Reference;
    PendingCommit pending{
        .status = ExecuteStatus::UnsupportedInstruction,
        .pc = frontend_result.pc,
        .instruction = frontend_result.instruction,
        .next_pc = frontend_result.pc,
    };
    if (reference || unit == ExecutionUnit::Integer) {
        pending = execute_decoded(
            frontend_result.decoded,
            frontend_result.pc,
            rs1_value,
            rs2_value);
    }

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

    if (!pending.ready() &&
        (reference || unit == ExecutionUnit::Privileged)) {
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

    if (!pending.ready() &&
        (reference || unit == ExecutionUnit::ControlFlow)) {
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

    if (!pending.ready() &&
        (reference || unit == ExecutionUnit::Atomic)) {
        const AtomicExecutionResult atomic_result =
            execute_atomic(
                *bus_,
                frontend_result.decoded,
                frontend_result.pc,
                state_.hart_id,
                rs1_value,
                rs2_value,
                &state_,
                execution_mode_ == ExecutionMode::Fast
                    ? &tlb_
                    : nullptr,
                execution_mode_ == ExecutionMode::Fast
                    ? &performance_counters_.mmu
                    : nullptr);

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

    if (!pending.ready() &&
        (reference || unit == ExecutionUnit::Csr)) {
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

    if (!pending.ready() &&
        (reference || unit == ExecutionUnit::Memory)) {
        const MemoryResult memory_result = execute_memory(
            *bus_,
            frontend_result.decoded,
            frontend_result.pc,
            rs1_value,
            rs2_value,
            &state_,
            execution_mode_ == ExecutionMode::Fast
                ? &tlb_
                : nullptr,
            execution_mode_ == ExecutionMode::Fast
                ? &performance_counters_.mmu
                : nullptr);

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

    if (frontend_result.decoded.kind ==
        InstructionKind::SfenceVma) {
        const std::optional<std::uint32_t> virtual_address =
            frontend_result.decoded.rs1 == 0U
                ? std::nullopt
                : std::optional<std::uint32_t>{rs1_value};
        const std::optional<std::uint16_t> asid =
            frontend_result.decoded.rs2 == 0U
                ? std::nullopt
                : std::optional<std::uint16_t>{
                      static_cast<std::uint16_t>(
                          rs2_value & 0x1FFU)};
        tlb_.sfence_vma(virtual_address, asid);
        instruction_cache_.sfence_vma(
            virtual_address,
            asid,
            &performance_counters_.instruction_cache);
    }
    if (frontend_result.decoded.kind ==
        InstructionKind::FenceI) {
        decode_cache_.clear(&performance_counters_.decode);
        instruction_cache_.clear(
            &performance_counters_.instruction_cache);
    }
    ++performance_counters_.retired_instructions;

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

const CorePerformanceCounters&
Core::performance_counters() const noexcept
{
    return performance_counters_;
}

std::size_t Core::tlb_entries() const noexcept
{
    return tlb_.valid_entries();
}

void Core::set_execution_mode(ExecutionMode mode) noexcept
{
    if (execution_mode_ == mode) {
        return;
    }
    execution_mode_ = mode;
    tlb_.clear();
    decode_cache_.clear();
    instruction_cache_.clear();
}

ExecutionMode Core::execution_mode() const noexcept
{
    return execution_mode_;
}

std::size_t Core::decoded_entries() const noexcept
{
    return decode_cache_.valid_entries();
}

std::size_t Core::instruction_cache_entries() const noexcept
{
    return instruction_cache_.valid_entries();
}

} // namespace rv32
