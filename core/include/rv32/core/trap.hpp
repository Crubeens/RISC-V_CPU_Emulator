#pragma once

#include <cstdint>

#include "rv32/core/decode.hpp"
#include "rv32/core/execute.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

enum class ExceptionCause : std::uint32_t {
    InstructionAddressMisaligned = 0,
    InstructionAccessFault = 1,
    IllegalInstruction = 2,
    Breakpoint = 3,
    LoadAddressMisaligned = 4,
    LoadAccessFault = 5,
    StoreAddressMisaligned = 6,
    StoreAccessFault = 7,
    EnvironmentCallFromUser = 8,
    EnvironmentCallFromSupervisor = 9,
    EnvironmentCallFromMachine = 11,
    InstructionPageFault = 12,
    LoadPageFault = 13,
    StorePageFault = 15,
};

enum class InterruptCause : std::uint32_t {
    SupervisorSoftware = 1,
    MachineSoftware = 3,
    SupervisorTimer = 5,
    MachineTimer = 7,
    SupervisorExternal = 9,
    MachineExternal = 11,
};

struct TrapRequest {
    ExceptionCause cause{ExceptionCause::IllegalInstruction};
    std::uint32_t exception_pc{};
    std::uint32_t trap_value{};
};

enum class TrapTarget : std::uint8_t {
    Machine,
    Supervisor,
};

struct InterruptTrapRequest {
    InterruptCause cause{InterruptCause::MachineExternal};
    std::uint32_t interrupted_pc{};
};

// Routes a synchronous exception according to the originating privilege and
// medeleg. Traps originating in M-mode are never delegated.
[[nodiscard]] TrapTarget take_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept;

// Commits a synchronous exception into the machine trap state. This is an
// architectural state transition, not a retired instruction.
void take_machine_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept;

void take_interrupt_trap(
    CpuSnapshot& state,
    const InterruptTrapRequest& request,
    TrapTarget target) noexcept;

[[nodiscard]] ExceptionCause environment_call_cause(
    PrivilegeMode privilege) noexcept;

enum class PrivilegedExecutionStatus : std::uint8_t {
    Ready,
    NotPrivilegedInstruction,
    IllegalInstruction,
};

struct PrivilegedExecutionResult {
    PrivilegedExecutionStatus status{
        PrivilegedExecutionStatus::NotPrivilegedInstruction};
    PendingCommit pending{};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == PrivilegedExecutionStatus::Ready;
    }
};

// Implements MRET, SRET, and WFI. State changes remain deferred in a
// PendingCommit so a failed validation cannot partially update the core.
[[nodiscard]] PrivilegedExecutionResult execute_privileged(
    const DecodedInstruction& decoded,
    const CpuSnapshot& state) noexcept;

} // namespace rv32
