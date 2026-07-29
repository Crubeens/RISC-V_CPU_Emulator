#pragma once

#include <array>
#include <cstdint>

#include "rv32/core/bus.hpp"

namespace rv32 {

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
    std::uint32_t reset_pc{0x80000000U};
    std::uint32_t hart_id{};
    PrivilegeMode initial_privilege{PrivilegeMode::Machine};
    // RISC-V boot convention: a0 receives hart_id and a1 receives this
    // platform-defined opaque argument (the DTB address for M5).
    std::uint32_t boot_argument{};
};

struct MachineCsrState {
    std::uint32_t mstatus{};
    std::uint32_t medeleg{};
    std::uint32_t mideleg{};
    std::uint32_t mie{};
    std::uint32_t mip_software{};
    std::uint32_t mip_lines{};
    std::uint32_t mcounteren{0x7U};
    std::uint32_t mtvec{};
    std::uint32_t mscratch{};
    std::uint32_t mepc{};
    std::uint32_t mcause{};
    std::uint32_t mtval{};

    [[nodiscard]] constexpr bool operator==(
        const MachineCsrState&) const noexcept = default;
};

struct SupervisorCsrState {
    std::uint32_t scounteren{0x7U};
    std::uint32_t stvec{};
    std::uint32_t sscratch{};
    std::uint32_t sepc{};
    std::uint32_t scause{};
    std::uint32_t stval{};
    std::uint32_t satp{};

    [[nodiscard]] constexpr bool operator==(
        const SupervisorCsrState&) const noexcept = default;
};

struct CpuSnapshot {
    std::array<std::uint32_t, 32> registers{};
    std::uint32_t pc{};
    std::uint32_t hart_id{};
    PrivilegeMode privilege{PrivilegeMode::Machine};
    MachineCsrState machine_csrs{};
    SupervisorCsrState supervisor_csrs{};
    std::uint64_t cycle{};
    std::uint64_t instructions_retired{};
    bool waiting_for_interrupt{};

    [[nodiscard]] constexpr bool operator==(
        const CpuSnapshot&) const noexcept = default;
};

enum class StepStatus : std::uint8_t {
    Retired,
    InstructionAddressMisaligned,
    InstructionAccessFault,
    IllegalInstruction,
    UnsupportedInstruction,
    EnvironmentCall,
    Breakpoint,
    LoadAddressMisaligned,
    LoadAccessFault,
    StoreAddressMisaligned,
    StoreAccessFault,
    InstructionPageFault,
    LoadPageFault,
    StorePageFault,
    TrapTaken,
    WaitingForInterrupt,
    CoreNotImplemented,
};

struct RegisterCommitTrace {
    bool enabled{};
    std::uint32_t index{};
    std::uint32_t value{};
};

// One architecturally retired instruction. Trap entry and idle WFI cycles do
// not produce a valid record; a WFI instruction itself does.
struct CommitTrace {
    bool valid{};
    PrivilegeMode privilege{PrivilegeMode::Machine};
    std::uint32_t pc{};
    std::uint32_t instruction{};
    std::uint32_t next_pc{};
    std::uint8_t instruction_length{};
    RegisterCommitTrace register_write{};
};

struct StepResult {
    StepStatus status{StepStatus::CoreNotImplemented};
    std::uint32_t pc{};
    std::uint32_t instruction{};
    std::uint32_t trap_value{};
    BusFault bus_fault{BusFault::None};
    std::uint64_t cycle{};
    CommitTrace commit{};
};

} // namespace rv32
