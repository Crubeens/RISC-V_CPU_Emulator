#pragma once

#include <cstdint>

#include "rv64/core/types.hpp"

namespace rv64 {

enum class ExceptionCause : Xlen {
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

enum class InterruptCause : Xlen {
    SupervisorSoftware = 1,
    MachineSoftware = 3,
    SupervisorTimer = 5,
    MachineTimer = 7,
    SupervisorExternal = 9,
    MachineExternal = 11,
};

enum class TrapTarget : std::uint8_t {
    Machine,
    Supervisor,
};

struct TrapRequest {
    ExceptionCause cause{ExceptionCause::IllegalInstruction};
    Xlen exception_pc{};
    Xlen trap_value{};
};

struct InterruptTrapRequest {
    InterruptCause cause{InterruptCause::MachineExternal};
    Xlen interrupted_pc{};
};

[[nodiscard]] TrapTarget take_trap(
    CpuSnapshot& state,
    const TrapRequest& request) noexcept;
void take_interrupt_trap(
    CpuSnapshot& state,
    const InterruptTrapRequest& request,
    TrapTarget target) noexcept;
[[nodiscard]] ExceptionCause environment_call_cause(
    PrivilegeMode privilege) noexcept;
[[nodiscard]] bool execute_mret(CpuSnapshot& state) noexcept;
[[nodiscard]] bool execute_sret(CpuSnapshot& state) noexcept;
[[nodiscard]] bool wfi_allowed(const CpuSnapshot& state) noexcept;
[[nodiscard]] bool sfence_vma_allowed(const CpuSnapshot& state) noexcept;

} // namespace rv64
