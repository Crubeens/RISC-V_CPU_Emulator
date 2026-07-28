#include "rv32/core/execute.hpp"

#include "rv32/core/mmu.hpp"

namespace rv32 {

namespace {

[[nodiscard]] constexpr std::int64_t as_signed_word(
    std::uint32_t value) noexcept
{
    constexpr std::int64_t word_modulus =
        std::int64_t{1} << 32U;
    if ((value & 0x80000000U) == 0U) {
        return static_cast<std::int64_t>(value);
    }
    return static_cast<std::int64_t>(value) - word_modulus;
}

[[nodiscard]] constexpr std::uint32_t high_word(
    std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>(value >> 32U);
}

[[nodiscard]] constexpr std::uint32_t multiply_high_signed(
    std::uint32_t lhs,
    std::uint32_t rhs) noexcept
{
    const std::int64_t product =
        as_signed_word(lhs) * as_signed_word(rhs);
    return high_word(static_cast<std::uint64_t>(product));
}

[[nodiscard]] constexpr std::uint32_t multiply_high_signed_unsigned(
    std::uint32_t lhs,
    std::uint32_t rhs) noexcept
{
    const std::int64_t product =
        as_signed_word(lhs) * static_cast<std::int64_t>(rhs);
    return high_word(static_cast<std::uint64_t>(product));
}

[[nodiscard]] constexpr std::uint32_t divide_signed(
    std::uint32_t dividend,
    std::uint32_t divisor) noexcept
{
    if (divisor == 0U) {
        return 0xFFFFFFFFU;
    }
    if (dividend == 0x80000000U &&
        divisor == 0xFFFFFFFFU) {
        return dividend;
    }

    return static_cast<std::uint32_t>(
        as_signed_word(dividend) / as_signed_word(divisor));
}

[[nodiscard]] constexpr std::uint32_t remainder_signed(
    std::uint32_t dividend,
    std::uint32_t divisor) noexcept
{
    if (divisor == 0U) {
        return dividend;
    }
    if (dividend == 0x80000000U &&
        divisor == 0xFFFFFFFFU) {
        return 0U;
    }

    return static_cast<std::uint32_t>(
        as_signed_word(dividend) % as_signed_word(divisor));
}

} // namespace

PendingCommit execute_decoded(
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value) noexcept
{
    const PendingCommit unsupported{
        .status = ExecuteStatus::UnsupportedInstruction,
        .pc = pc,
        .instruction = decoded.raw,
        .next_pc = pc,
        .register_write = {},
    };

    if (!decoded.valid()) {
        return unsupported;
    }

    PendingCommit pending{
        .status = ExecuteStatus::Ready,
        .pc = pc,
        .instruction = decoded.raw,
        .next_pc = pc + decoded.length,
        .register_write = {
            .enabled = true,
            .index = decoded.rd,
            .value = 0,
        },
    };

    std::uint32_t& value = pending.register_write.value;
    const std::uint32_t immediate_shift =
        decoded.immediate & 0x1FU;
    const std::uint32_t register_shift =
        rs2_value & 0x1FU;

    switch (decoded.kind) {
    case InstructionKind::Lui:
        value = decoded.immediate;
        break;
    case InstructionKind::Addi:
        value = rs1_value + decoded.immediate;
        break;
    case InstructionKind::Slti:
        value =
            static_cast<std::int32_t>(rs1_value) <
                    static_cast<std::int32_t>(decoded.immediate)
                ? 1U
                : 0U;
        break;
    case InstructionKind::Sltiu:
        value = rs1_value < decoded.immediate ? 1U : 0U;
        break;
    case InstructionKind::Xori:
        value = rs1_value ^ decoded.immediate;
        break;
    case InstructionKind::Ori:
        value = rs1_value | decoded.immediate;
        break;
    case InstructionKind::Andi:
        value = rs1_value & decoded.immediate;
        break;
    case InstructionKind::Slli:
        value = rs1_value << immediate_shift;
        break;
    case InstructionKind::Srli:
        value = rs1_value >> immediate_shift;
        break;
    case InstructionKind::Srai:
        value = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(rs1_value) >>
            immediate_shift);
        break;
    case InstructionKind::Add:
        value = rs1_value + rs2_value;
        break;
    case InstructionKind::Sub:
        value = rs1_value - rs2_value;
        break;
    case InstructionKind::Sll:
        value = rs1_value << register_shift;
        break;
    case InstructionKind::Slt:
        value =
            static_cast<std::int32_t>(rs1_value) <
                    static_cast<std::int32_t>(rs2_value)
                ? 1U
                : 0U;
        break;
    case InstructionKind::Sltu:
        value = rs1_value < rs2_value ? 1U : 0U;
        break;
    case InstructionKind::Xor:
        value = rs1_value ^ rs2_value;
        break;
    case InstructionKind::Srl:
        value = rs1_value >> register_shift;
        break;
    case InstructionKind::Sra:
        value = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(rs1_value) >>
            register_shift);
        break;
    case InstructionKind::Or:
        value = rs1_value | rs2_value;
        break;
    case InstructionKind::And:
        value = rs1_value & rs2_value;
        break;
    case InstructionKind::Mul:
        value = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(rs1_value) *
            static_cast<std::uint64_t>(rs2_value));
        break;
    case InstructionKind::Mulh:
        value = multiply_high_signed(rs1_value, rs2_value);
        break;
    case InstructionKind::Mulhsu:
        value =
            multiply_high_signed_unsigned(rs1_value, rs2_value);
        break;
    case InstructionKind::Mulhu:
        value = high_word(
            static_cast<std::uint64_t>(rs1_value) *
            static_cast<std::uint64_t>(rs2_value));
        break;
    case InstructionKind::Div:
        value = divide_signed(rs1_value, rs2_value);
        break;
    case InstructionKind::Divu:
        value = rs2_value == 0U
                    ? 0xFFFFFFFFU
                    : rs1_value / rs2_value;
        break;
    case InstructionKind::Rem:
        value = remainder_signed(rs1_value, rs2_value);
        break;
    case InstructionKind::Remu:
        value = rs2_value == 0U
                    ? rs1_value
                    : rs1_value % rs2_value;
        break;
    case InstructionKind::Fence:
    case InstructionKind::FenceI:
        pending.register_write = {};
        break;
    case InstructionKind::Ecall:
        return {
            .status = ExecuteStatus::EnvironmentCall,
            .pc = pc,
            .instruction = decoded.raw,
            .next_pc = pc,
            .register_write = {},
        };
    case InstructionKind::Ebreak:
        return {
            .status = ExecuteStatus::Breakpoint,
            .pc = pc,
            .instruction = decoded.raw,
            .next_pc = pc,
            .register_write = {},
        };
    default:
        return unsupported;
    }

    return pending;
}

ControlFlowResult execute_control_flow(
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value) noexcept
{
    const PendingCommit not_ready{
        .status = ExecuteStatus::UnsupportedInstruction,
        .pc = pc,
        .instruction = decoded.raw,
        .next_pc = pc,
        .register_write = {},
    };
    const auto not_control_flow = [&]() noexcept {
        return ControlFlowResult{
            .status = ControlFlowStatus::NotControlFlowInstruction,
            .pending = not_ready,
            .trap_value = 0,
        };
    };
    const auto misaligned = [&](std::uint32_t target) noexcept {
        return ControlFlowResult{
            .status = ControlFlowStatus::InstructionAddressMisaligned,
            .pending = not_ready,
            .trap_value = target,
        };
    };
    const auto ready =
        [&](std::uint32_t next_pc,
            bool write_register,
            std::uint32_t register_value) noexcept {
            return ControlFlowResult{
                .status = ControlFlowStatus::Ready,
                .pending = {
                    .status = ExecuteStatus::Ready,
                    .pc = pc,
                    .instruction = decoded.raw,
                    .next_pc = next_pc,
                    .register_write = {
                        .enabled = write_register,
                        .index = decoded.rd,
                        .value = register_value,
                    },
                },
                .trap_value = 0,
            };
        };
    const auto finish_target =
        [&](std::uint32_t target,
            bool write_register,
            std::uint32_t register_value) noexcept {
            if ((target & 0x1U) != 0U) {
                return misaligned(target);
            }
            return ready(target, write_register, register_value);
        };

    switch (decoded.kind) {
    case InstructionKind::Auipc:
        return ready(
            pc + decoded.length,
            true,
            pc + decoded.immediate);
    case InstructionKind::Jal:
        return finish_target(
            pc + decoded.immediate,
            true,
            pc + decoded.length);
    case InstructionKind::Jalr:
        return finish_target(
            (rs1_value + decoded.immediate) & ~std::uint32_t{1},
            true,
            pc + decoded.length);
    case InstructionKind::Beq:
        if (rs1_value == rs2_value) {
            return finish_target(
                pc + decoded.immediate,
                false,
                0);
        }
        return ready(pc + decoded.length, false, 0);
    case InstructionKind::Bne:
        if (rs1_value != rs2_value) {
            return finish_target(
                pc + decoded.immediate,
                false,
                0);
        }
        return ready(pc + decoded.length, false, 0);
    case InstructionKind::Blt:
        if (static_cast<std::int32_t>(rs1_value) <
            static_cast<std::int32_t>(rs2_value)) {
            return finish_target(
                pc + decoded.immediate,
                false,
                0);
        }
        return ready(pc + decoded.length, false, 0);
    case InstructionKind::Bge:
        if (static_cast<std::int32_t>(rs1_value) >=
            static_cast<std::int32_t>(rs2_value)) {
            return finish_target(
                pc + decoded.immediate,
                false,
                0);
        }
        return ready(pc + decoded.length, false, 0);
    case InstructionKind::Bltu:
        if (rs1_value < rs2_value) {
            return finish_target(
                pc + decoded.immediate,
                false,
                0);
        }
        return ready(pc + decoded.length, false, 0);
    case InstructionKind::Bgeu:
        if (rs1_value >= rs2_value) {
            return finish_target(
                pc + decoded.immediate,
                false,
                0);
        }
        return ready(pc + decoded.length, false, 0);
    default:
        return not_control_flow();
    }
}

MemoryResult execute_memory(
    CpuBus& bus,
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value,
    const CpuSnapshot* state)
{
    const std::uint32_t address =
        rs1_value + decoded.immediate;

    const PendingCommit not_ready{
        .status = ExecuteStatus::UnsupportedInstruction,
        .pc = pc,
        .instruction = decoded.raw,
        .next_pc = pc,
        .register_write = {},
    };
    const auto failure =
        [&](MemoryStatus status, BusFault fault) {
            return MemoryResult{
                .status = status,
                .pending = not_ready,
                .bus_fault = fault,
                .trap_value = address,
            };
        };

    AccessWidth width = AccessWidth::Byte;
    bool is_load = false;

    switch (decoded.kind) {
    case InstructionKind::Lb:
    case InstructionKind::Lbu:
        width = AccessWidth::Byte;
        is_load = true;
        break;
    case InstructionKind::Lh:
    case InstructionKind::Lhu:
        width = AccessWidth::HalfWord;
        is_load = true;
        break;
    case InstructionKind::Lw:
        width = AccessWidth::Word;
        is_load = true;
        break;
    case InstructionKind::Sb:
        width = AccessWidth::Byte;
        break;
    case InstructionKind::Sh:
        width = AccessWidth::HalfWord;
        break;
    case InstructionKind::Sw:
        width = AccessWidth::Word;
        break;
    default:
        return {
            .status = MemoryStatus::NotMemoryInstruction,
            .pending = not_ready,
            .bus_fault = BusFault::None,
            .trap_value = 0,
        };
    }

    const std::uint32_t alignment =
        static_cast<std::uint32_t>(width_bytes(width));
    if ((address & (alignment - 1U)) != 0U) {
        return failure(
            is_load
                ? MemoryStatus::LoadAddressMisaligned
                : MemoryStatus::StoreAddressMisaligned,
            BusFault::Misaligned);
    }

    PhysAddr physical_address = static_cast<PhysAddr>(address);
    if (state != nullptr) {
        const TranslationResult translation =
            translate_address(
                bus,
                *state,
                address,
                is_load
                    ? MemoryAccessType::Load
                    : MemoryAccessType::Store);
        if (!translation.ready()) {
            return failure(
                translation.status == TranslationStatus::PageFault
                    ? (is_load
                           ? MemoryStatus::LoadPageFault
                           : MemoryStatus::StorePageFault)
                    : (is_load
                           ? MemoryStatus::LoadAccessFault
                           : MemoryStatus::StoreAccessFault),
                translation.bus_fault);
        }
        physical_address = translation.physical_address;
    }

    if (is_load) {
        const ReadResult read_result = bus.read(
            physical_address,
            width,
            AccessKind::Load);
        if (!read_result.ok()) {
            return failure(
                read_result.fault == BusFault::Misaligned
                    ? MemoryStatus::LoadAddressMisaligned
                    : MemoryStatus::LoadAccessFault,
                read_result.fault);
        }

        const std::uint32_t raw_value =
            static_cast<std::uint32_t>(read_result.value);
        std::uint32_t loaded_value = 0;
        switch (decoded.kind) {
        case InstructionKind::Lb:
            loaded_value = sign_extend(raw_value, 8U);
            break;
        case InstructionKind::Lbu:
            loaded_value = raw_value & 0xFFU;
            break;
        case InstructionKind::Lh:
            loaded_value = sign_extend(raw_value, 16U);
            break;
        case InstructionKind::Lhu:
            loaded_value = raw_value & 0xFFFFU;
            break;
        case InstructionKind::Lw:
            loaded_value = raw_value;
            break;
        default:
            return failure(
                MemoryStatus::LoadAccessFault,
                BusFault::DeviceError);
        }

        return {
            .status = MemoryStatus::Ready,
            .pending = {
                .status = ExecuteStatus::Ready,
                .pc = pc,
                .instruction = decoded.raw,
                .next_pc = pc + decoded.length,
                .register_write = {
                    .enabled = true,
                    .index = decoded.rd,
                    .value = loaded_value,
                },
            },
            .bus_fault = BusFault::None,
            .trap_value = 0,
        };
    }

    const BusFault write_fault = bus.write(
        physical_address,
        width,
        rs2_value,
        AccessKind::Store);
    if (write_fault != BusFault::None) {
        return failure(
            write_fault == BusFault::Misaligned
                ? MemoryStatus::StoreAddressMisaligned
                : MemoryStatus::StoreAccessFault,
            write_fault);
    }

    return {
        .status = MemoryStatus::Ready,
        .pending = {
            .status = ExecuteStatus::Ready,
            .pc = pc,
            .instruction = decoded.raw,
            .next_pc = pc + decoded.length,
            .register_write = {},
        },
        .bus_fault = BusFault::None,
        .trap_value = 0,
    };
}

AtomicExecutionResult execute_atomic(
    CpuBus& bus,
    const DecodedInstruction& decoded,
    std::uint32_t pc,
    std::uint32_t hart_id,
    std::uint32_t rs1_value,
    std::uint32_t rs2_value,
    const CpuSnapshot* state)
{
    const PendingCommit not_ready{
        .status = ExecuteStatus::UnsupportedInstruction,
        .pc = pc,
        .instruction = decoded.raw,
        .next_pc = pc,
        .register_write = {},
    };
    const auto not_atomic = [&]() {
        return AtomicExecutionResult{
            .status = AtomicStatus::NotAtomicInstruction,
            .pending = not_ready,
            .bus_fault = BusFault::None,
            .trap_value = 0,
        };
    };
    const auto failure =
        [&](AtomicStatus status, BusFault fault) {
            return AtomicExecutionResult{
                .status = status,
                .pending = not_ready,
                .bus_fault = fault,
                .trap_value = rs1_value,
            };
        };
    const auto ready = [&](std::uint32_t value) {
        return AtomicExecutionResult{
            .status = AtomicStatus::Ready,
            .pending = {
                .status = ExecuteStatus::Ready,
                .pc = pc,
                .instruction = decoded.raw,
                .next_pc = pc + decoded.length,
                .register_write = {
                    .enabled = true,
                    .index = decoded.rd,
                    .value = value,
                },
            },
            .bus_fault = BusFault::None,
            .trap_value = 0,
        };
    };

    bool is_load_reserved = false;
    bool is_store_conditional = false;
    bool is_amo = false;
    AmoOperation operation = AmoOperation::Swap;

    switch (decoded.kind) {
    case InstructionKind::LrW:
        is_load_reserved = true;
        break;
    case InstructionKind::ScW:
        is_store_conditional = true;
        break;
    case InstructionKind::AmoSwapW:
        is_amo = true;
        operation = AmoOperation::Swap;
        break;
    case InstructionKind::AmoAddW:
        is_amo = true;
        operation = AmoOperation::Add;
        break;
    case InstructionKind::AmoXorW:
        is_amo = true;
        operation = AmoOperation::Xor;
        break;
    case InstructionKind::AmoAndW:
        is_amo = true;
        operation = AmoOperation::And;
        break;
    case InstructionKind::AmoOrW:
        is_amo = true;
        operation = AmoOperation::Or;
        break;
    case InstructionKind::AmoMinW:
        is_amo = true;
        operation = AmoOperation::Min;
        break;
    case InstructionKind::AmoMaxW:
        is_amo = true;
        operation = AmoOperation::Max;
        break;
    case InstructionKind::AmoMinuW:
        is_amo = true;
        operation = AmoOperation::MinUnsigned;
        break;
    case InstructionKind::AmoMaxuW:
        is_amo = true;
        operation = AmoOperation::MaxUnsigned;
        break;
    default:
        return not_atomic();
    }

    if ((rs1_value & 0x3U) != 0U) {
        return failure(
            is_load_reserved
                ? AtomicStatus::LoadAddressMisaligned
                : AtomicStatus::StoreAddressMisaligned,
            BusFault::Misaligned);
    }

    PhysAddr address = static_cast<PhysAddr>(rs1_value);
    if (state != nullptr) {
        const TranslationResult translation =
            translate_address(
                bus,
                *state,
                rs1_value,
                is_load_reserved
                    ? MemoryAccessType::Load
                    : MemoryAccessType::Store);
        if (!translation.ready()) {
            return failure(
                translation.status == TranslationStatus::PageFault
                    ? (is_load_reserved
                           ? AtomicStatus::LoadPageFault
                           : AtomicStatus::StorePageFault)
                    : (is_load_reserved
                           ? AtomicStatus::LoadAccessFault
                           : AtomicStatus::StoreAccessFault),
                translation.bus_fault);
        }
        address = translation.physical_address;
    }
    if (is_load_reserved) {
        const ReadResult result =
            bus.load_reserved_word(hart_id, address);
        if (!result.ok()) {
            return failure(
                result.fault == BusFault::Misaligned
                    ? AtomicStatus::LoadAddressMisaligned
                    : AtomicStatus::LoadAccessFault,
                result.fault);
        }
        return ready(static_cast<std::uint32_t>(result.value));
    }

    if (is_store_conditional) {
        const StoreConditionalResult result =
            bus.store_conditional_word(
                hart_id,
                address,
                rs2_value);
        if (!result.ok()) {
            return failure(
                result.fault == BusFault::Misaligned
                    ? AtomicStatus::StoreAddressMisaligned
                    : AtomicStatus::StoreAccessFault,
                result.fault);
        }
        return ready(result.succeeded ? 0U : 1U);
    }

    if (is_amo) {
        const AtomicResult result = bus.atomic_word(
            hart_id,
            address,
            operation,
            rs2_value);
        if (!result.ok()) {
            return failure(
                result.fault == BusFault::Misaligned
                    ? AtomicStatus::StoreAddressMisaligned
                    : AtomicStatus::StoreAccessFault,
                result.fault);
        }
        return ready(result.original_value);
    }

    return not_atomic();
}

CsrExecutionResult execute_csr(
    CsrAccess& csr_access,
    const DecodedInstruction& decoded,
    PrivilegeMode privilege,
    std::uint32_t pc,
    std::uint32_t rs1_value) noexcept
{
    const PendingCommit not_ready{
        .status = ExecuteStatus::UnsupportedInstruction,
        .pc = pc,
        .instruction = decoded.raw,
        .next_pc = pc,
        .register_write = {},
        .csr_write = {},
    };
    const auto not_csr = [&]() noexcept {
        return CsrExecutionResult{
            .status = CsrExecutionStatus::NotCsrInstruction,
            .pending = not_ready,
            .access_status = CsrAccessStatus::Ready,
            .trap_value = 0,
        };
    };
    const auto illegal =
        [&](CsrAccessStatus access_status) noexcept {
            return CsrExecutionResult{
                .status = CsrExecutionStatus::IllegalInstruction,
                .pending = not_ready,
                .access_status = access_status,
                .trap_value = decoded.raw,
            };
        };

    bool read_required = false;
    bool write_required = false;
    std::uint32_t source_value = 0;

    switch (decoded.kind) {
    case InstructionKind::Csrrw:
        read_required = decoded.rd != 0U;
        write_required = true;
        source_value = rs1_value;
        break;
    case InstructionKind::Csrrs:
    case InstructionKind::Csrrc:
        read_required = true;
        write_required = decoded.rs1 != 0U;
        source_value = rs1_value;
        break;
    case InstructionKind::Csrrwi:
        read_required = decoded.rd != 0U;
        write_required = true;
        source_value = decoded.immediate;
        break;
    case InstructionKind::Csrrsi:
    case InstructionKind::Csrrci:
        read_required = true;
        write_required = decoded.immediate != 0U;
        source_value = decoded.immediate;
        break;
    default:
        return not_csr();
    }

    if (write_required) {
        const CsrAccessStatus status =
            csr_access.validate_write(decoded.csr, privilege);
        if (status != CsrAccessStatus::Ready) {
            return illegal(status);
        }
    }

    std::uint32_t old_value = 0;
    if (read_required) {
        const CsrReadResult result =
            csr_access.read(decoded.csr, privilege);
        if (!result.ready()) {
            return illegal(result.status);
        }
        old_value = result.value;
    }

    std::uint32_t modification_base = old_value;
    if (write_required &&
        (decoded.kind == InstructionKind::Csrrs ||
         decoded.kind == InstructionKind::Csrrsi ||
         decoded.kind == InstructionKind::Csrrc ||
         decoded.kind == InstructionKind::Csrrci)) {
        modification_base =
            csr_access.read_for_write(decoded.csr, old_value);
    }

    std::uint32_t new_value = source_value;
    if (decoded.kind == InstructionKind::Csrrs ||
        decoded.kind == InstructionKind::Csrrsi) {
        new_value = modification_base | source_value;
    } else if (decoded.kind == InstructionKind::Csrrc ||
               decoded.kind == InstructionKind::Csrrci) {
        new_value = modification_base & ~source_value;
    }

    return {
        .status = CsrExecutionStatus::Ready,
        .pending = {
            .status = ExecuteStatus::Ready,
            .pc = pc,
            .instruction = decoded.raw,
            .next_pc = pc + decoded.length,
            .register_write = {
                .enabled = decoded.rd != 0U,
                .index = decoded.rd,
                .value = old_value,
            },
            .csr_write = {
                .enabled = write_required,
                .address = decoded.csr,
                .value = new_value,
            },
        },
        .access_status = CsrAccessStatus::Ready,
        .trap_value = 0,
    };
}

bool commit_pending(
    CpuSnapshot& state,
    const PendingCommit& pending,
    CsrAccess* csr_access) noexcept
{
    if (!pending.ready()) {
        return false;
    }
    if (pending.pc != state.pc) {
        return false;
    }
    if (pending.register_write.enabled &&
        pending.register_write.index >= state.registers.size()) {
        return false;
    }
    if (pending.privilege_write.enabled) {
        switch (pending.privilege_write.value) {
        case PrivilegeMode::User:
        case PrivilegeMode::Supervisor:
        case PrivilegeMode::Machine:
            break;
        default:
            return false;
        }
    }
    if (pending.csr_write.enabled) {
        if (csr_access == nullptr ||
            csr_access->validate_write(
                pending.csr_write.address,
                state.privilege) != CsrAccessStatus::Ready) {
            return false;
        }
    }

    if (pending.register_write.enabled &&
        pending.register_write.index != 0U) {
        state.registers[pending.register_write.index] =
            pending.register_write.value;
    }
    if (pending.privilege_write.enabled) {
        state.privilege = pending.privilege_write.value;
    }
    state.pc = pending.next_pc;
    ++state.instructions_retired;
    state.waiting_for_interrupt = pending.wait_for_interrupt;
    state.registers[0] = 0;
    // CSR writes take effect after the writing instruction has otherwise
    // completed. This is observable for minstret/minstreth writes.
    if (pending.csr_write.enabled) {
        csr_access->write_validated(
            pending.csr_write.address,
            pending.csr_write.value);
    }
    if (pending.privilege_write.enabled &&
        pending.privilege_write.value != PrivilegeMode::Machine) {
        state.machine_csrs.mstatus &= ~mstatus_bits::mprv;
    }
    return true;
}

} // namespace rv32
