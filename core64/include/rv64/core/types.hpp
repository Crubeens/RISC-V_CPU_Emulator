#pragma once

#include <array>
#include <cstdint>

#include "rv/common/bus.hpp"

namespace rv64 {

using Xlen = std::uint64_t;

enum class PrivilegeMode : std::uint8_t {
    User = 0,
    Supervisor = 1,
    Machine = 3,
};

struct IrqLines {
    bool machine_software{};
    bool machine_timer{};
    bool machine_external{};
    bool supervisor_software{};
    bool supervisor_timer{};
    bool supervisor_external{};
};

struct ResetConfig {
    Xlen reset_pc{0x80000000ULL};
    Xlen hart_id{};
    PrivilegeMode initial_privilege{PrivilegeMode::Machine};
    Xlen boot_argument{};
};

struct MachineCsrState {
    Xlen mstatus{};
    Xlen medeleg{};
    Xlen mideleg{};
    Xlen mie{};
    Xlen mip_software{};
    Xlen mip_lines{};
    Xlen mcounteren{0x7U};
    Xlen mtvec{};
    Xlen mscratch{};
    Xlen mepc{};
    Xlen mcause{};
    Xlen mtval{};

    [[nodiscard]] constexpr bool operator==(
        const MachineCsrState&) const noexcept = default;
};

struct SupervisorCsrState {
    Xlen scounteren{0x7U};
    Xlen stvec{};
    Xlen sscratch{};
    Xlen sepc{};
    Xlen scause{};
    Xlen stval{};
    Xlen satp{};

    [[nodiscard]] constexpr bool operator==(
        const SupervisorCsrState&) const noexcept = default;
};

struct FloatingPointState {
    std::array<Xlen, 32> registers{};
    std::uint8_t fcsr{};

    [[nodiscard]] constexpr bool operator==(
        const FloatingPointState&) const noexcept = default;
};

struct CpuSnapshot {
    std::array<Xlen, 32> registers{};
    FloatingPointState floating_point{};
    Xlen pc{};
    Xlen hart_id{};
    PrivilegeMode privilege{PrivilegeMode::Machine};
    MachineCsrState machine_csrs{};
    SupervisorCsrState supervisor_csrs{};
    Xlen cycle{};
    Xlen instructions_retired{};
    bool waiting_for_interrupt{};

    [[nodiscard]] constexpr bool operator==(
        const CpuSnapshot&) const noexcept = default;
};

enum class StepStatus : std::uint8_t {
    Retired,
    InstructionAddressMisaligned,
    InstructionAccessFault,
    IllegalInstruction,
    EnvironmentCall,
    Breakpoint,
    LoadAddressMisaligned,
    LoadAccessFault,
    StoreAddressMisaligned,
    StoreAccessFault,
    TrapTaken,
    WaitingForInterrupt,
};

struct RegisterCommit {
    bool enabled{};
    std::uint8_t index{};
    Xlen value{};
};

struct FloatingRegisterCommit {
    bool enabled{};
    std::uint8_t index{};
    Xlen value{};
};

struct StepResult {
    StepStatus status{StepStatus::IllegalInstruction};
    PrivilegeMode privilege{PrivilegeMode::Machine};
    Xlen pc{};
    std::uint32_t instruction{};
    Xlen trap_value{};
    rv::BusFault bus_fault{rv::BusFault::None};
    RegisterCommit register_write{};
    FloatingRegisterCommit floating_register_write{};
};

} // namespace rv64
