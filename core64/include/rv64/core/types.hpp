#pragma once

#include <array>
#include <cstdint>

#include "rv/common/bus.hpp"

namespace rv64 {

using Xlen = std::uint64_t;

struct ResetConfig {
    Xlen reset_pc{0x80000000ULL};
    Xlen hart_id{};
    Xlen boot_argument{};
};

struct CpuSnapshot {
    std::array<Xlen, 32> registers{};
    Xlen pc{};
    Xlen cycle{};
    Xlen instructions_retired{};
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
};

struct RegisterCommit {
    bool enabled{};
    std::uint8_t index{};
    Xlen value{};
};

struct StepResult {
    StepStatus status{StepStatus::IllegalInstruction};
    Xlen pc{};
    std::uint32_t instruction{};
    Xlen trap_value{};
    rv::BusFault bus_fault{rv::BusFault::None};
    RegisterCommit register_write{};
};

} // namespace rv64
