#pragma once

#include <cstdint>

#include "rv32/core/csr.hpp"
#include "rv32/core/decode.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

enum class ExecuteStatus : std::uint8_t {
    Ready,
    UnsupportedInstruction,
    EnvironmentCall,
    Breakpoint,
};

struct RegisterWrite {
    bool enabled{};
    std::uint32_t index{};
    std::uint32_t value{};
};

struct CsrWrite {
    bool enabled{};
    CsrAddress address{};
    std::uint32_t value{};
};

struct PrivilegeWrite {
    bool enabled{};
    PrivilegeMode value{PrivilegeMode::Machine};
};

struct PendingCommit {
    ExecuteStatus status{ExecuteStatus::UnsupportedInstruction};
    std::uint32_t pc{};
    std::uint32_t instruction{};
    std::uint32_t next_pc{};
    RegisterWrite register_write{};
    CsrWrite csr_write{};
    PrivilegeWrite privilege_write{};
    bool wait_for_interrupt{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == ExecuteStatus::Ready;
    }
};

enum class ControlFlowStatus : std::uint8_t {
    Ready,
    NotControlFlowInstruction,
    InstructionAddressMisaligned,
};

struct ControlFlowResult {
    ControlFlowStatus status{
        ControlFlowStatus::NotControlFlowInstruction};
    PendingCommit pending{};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == ControlFlowStatus::Ready;
    }
};

enum class MemoryStatus : std::uint8_t {
    Ready,
    NotMemoryInstruction,
    LoadAddressMisaligned,
    LoadAccessFault,
    StoreAddressMisaligned,
    StoreAccessFault,
};

struct MemoryResult {
    MemoryStatus status{MemoryStatus::NotMemoryInstruction};
    PendingCommit pending{};
    BusFault bus_fault{BusFault::None};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == MemoryStatus::Ready;
    }
};

enum class AtomicStatus : std::uint8_t {
    Ready,
    NotAtomicInstruction,
    LoadAddressMisaligned,
    LoadAccessFault,
    StoreAddressMisaligned,
    StoreAccessFault,
};

struct AtomicExecutionResult {
    AtomicStatus status{AtomicStatus::NotAtomicInstruction};
    PendingCommit pending{};
    BusFault bus_fault{BusFault::None};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == AtomicStatus::Ready;
    }
};

enum class CsrExecutionStatus : std::uint8_t {
    Ready,
    NotCsrInstruction,
    IllegalInstruction,
};

struct CsrExecutionResult {
    CsrExecutionStatus status{CsrExecutionStatus::NotCsrInstruction};
    PendingCommit pending{};
    CsrAccessStatus access_status{CsrAccessStatus::Ready};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == CsrExecutionStatus::Ready;
    }
};

// M1.4 begins with the non-memory integer execution slice. This function is
// pure: it may calculate a PendingCommit, but it must not modify CPU state or
// access the system bus.
[[nodiscard]] PendingCommit execute_decoded(
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value) noexcept;

// Executes AUIPC, jumps, and conditional branches without changing CPU state.
// A taken target must satisfy RV32I IALIGN=32 before a commit is produced.
[[nodiscard]] ControlFlowResult execute_control_flow(
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value) noexcept;

// Executes only RV32I loads and stores. A failed or misaligned access must not
// produce a ready PendingCommit, and every real bus access occurs exactly once.
[[nodiscard]] MemoryResult execute_memory(
    CpuBus& bus,
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value);

// Executes RV32A word atomics only through CpuBus. The synchronous, in-order
// core already serializes memory accesses, while acquire/release bits remain
// available in DecodedInstruction for a future parallel execution engine.
[[nodiscard]] AtomicExecutionResult execute_atomic(
    CpuBus& bus,
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t hart_id,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value);

// Executes all six Zicsr instruction forms against an abstract CSR file.
// Writes are validated here but deferred into PendingCommit for precise state
// updates.
[[nodiscard]] CsrExecutionResult execute_csr(
    CsrAccess& csr_access,
    const DecodedInstruction& decoded,
    PrivilegeMode privilege,
    std::uint32_t pc,
    std::uint32_t rs1_value) noexcept;

// Applies a fully validated result in one architectural commit. It must reject
// a non-ready, stale, or malformed result without partially changing state.
[[nodiscard]] bool commit_pending(
    CpuSnapshot& state,
    const PendingCommit& pending,
    CsrAccess* csr_access = nullptr) noexcept;

} // namespace rv32
