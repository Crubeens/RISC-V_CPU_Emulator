#include "rv64/core/core.hpp"

#include <bit>
#include <cstdint>
#include <optional>

#include "rv64/core/decode.hpp"

namespace rv64 {

namespace {

[[nodiscard]] constexpr std::uint64_t sign_extend(
    std::uint64_t value,
    unsigned int width) noexcept
{
    if (width == 64U) {
        return value;
    }
    const std::uint64_t sign = std::uint64_t{1} << (width - 1U);
    const std::uint64_t mask = (std::uint64_t{1} << width) - 1U;
    const std::uint64_t masked = value & mask;
    return (masked ^ sign) - sign;
}

[[nodiscard]] constexpr std::uint64_t sign_extend_word(
    std::uint64_t value) noexcept
{
    return sign_extend(value, 32U);
}

[[nodiscard]] std::int64_t as_signed(std::uint64_t value) noexcept
{
    return std::bit_cast<std::int64_t>(value);
}

[[nodiscard]] std::int32_t as_signed_word(std::uint64_t value) noexcept
{
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(value));
}

[[nodiscard]] constexpr bool aligned(
    std::uint64_t address,
    rv::AccessWidth width) noexcept
{
    const std::uint64_t bytes = rv::width_bytes(width);
    return (address & (bytes - 1U)) == 0U;
}

[[nodiscard]] StepResult failure(
    StepStatus status,
    std::uint64_t pc,
    std::uint32_t instruction,
    std::uint64_t trap_value,
    rv::BusFault bus_fault = rv::BusFault::None) noexcept
{
    return {
        .status = status,
        .pc = pc,
        .instruction = instruction,
        .trap_value = trap_value,
        .bus_fault = bus_fault,
    };
}

} // namespace

Core::Core(rv::CpuBus& bus) noexcept : bus_(&bus)
{
    reset();
}

void Core::reset(const ResetConfig& config) noexcept
{
    state_ = {};
    state_.pc = config.reset_pc;
    state_.registers[10] = config.hart_id;
    state_.registers[11] = config.boot_argument;
}

StepResult Core::step()
{
    const std::uint64_t pc = state_.pc;
    ++state_.cycle;
    state_.registers[0] = 0;

    if ((pc & 0x3U) != 0U) {
        return failure(
            StepStatus::InstructionAddressMisaligned,
            pc,
            0,
            pc,
            rv::BusFault::Misaligned);
    }

    const rv::ReadResult fetched = bus_->read(
        pc,
        rv::AccessWidth::Word,
        rv::AccessKind::InstructionFetch);
    if (!fetched.ok()) {
        return failure(
            StepStatus::InstructionAccessFault,
            pc,
            0,
            pc,
            fetched.fault);
    }

    const std::uint32_t instruction =
        static_cast<std::uint32_t>(fetched.value);
    const DecodedInstruction decoded =
        decode_instruction(instruction);
    if (!decoded.valid()) {
        return failure(
            StepStatus::IllegalInstruction,
            pc,
            instruction,
            instruction);
    }

    const std::uint64_t rs1 = state_.registers[decoded.rs1];
    const std::uint64_t rs2 = state_.registers[decoded.rs2];
    std::uint64_t next_pc = pc + 4U;
    std::optional<std::uint64_t> register_value;

    const auto branch = [&](bool take) {
        if (take) {
            next_pc = pc + decoded.immediate;
        }
    };

    switch (decoded.kind) {
    case InstructionKind::Lui:
        register_value = decoded.immediate;
        break;
    case InstructionKind::Auipc:
        register_value = pc + decoded.immediate;
        break;
    case InstructionKind::Jal:
        register_value = next_pc;
        next_pc = pc + decoded.immediate;
        break;
    case InstructionKind::Jalr:
        register_value = next_pc;
        next_pc = (rs1 + decoded.immediate) & ~std::uint64_t{1};
        break;
    case InstructionKind::Beq:
        branch(rs1 == rs2);
        break;
    case InstructionKind::Bne:
        branch(rs1 != rs2);
        break;
    case InstructionKind::Blt:
        branch(as_signed(rs1) < as_signed(rs2));
        break;
    case InstructionKind::Bge:
        branch(as_signed(rs1) >= as_signed(rs2));
        break;
    case InstructionKind::Bltu:
        branch(rs1 < rs2);
        break;
    case InstructionKind::Bgeu:
        branch(rs1 >= rs2);
        break;
    case InstructionKind::Addi:
        register_value = rs1 + decoded.immediate;
        break;
    case InstructionKind::Slti:
        register_value =
            as_signed(rs1) < as_signed(decoded.immediate) ? 1U : 0U;
        break;
    case InstructionKind::Sltiu:
        register_value = rs1 < decoded.immediate ? 1U : 0U;
        break;
    case InstructionKind::Xori:
        register_value = rs1 ^ decoded.immediate;
        break;
    case InstructionKind::Ori:
        register_value = rs1 | decoded.immediate;
        break;
    case InstructionKind::Andi:
        register_value = rs1 & decoded.immediate;
        break;
    case InstructionKind::Slli:
        register_value = rs1 << (decoded.immediate & 0x3FU);
        break;
    case InstructionKind::Srli:
        register_value = rs1 >> (decoded.immediate & 0x3FU);
        break;
    case InstructionKind::Srai:
        register_value = std::bit_cast<std::uint64_t>(
            as_signed(rs1) >> (decoded.immediate & 0x3FU));
        break;
    case InstructionKind::Addiw:
        register_value =
            sign_extend_word(rs1 + decoded.immediate);
        break;
    case InstructionKind::Slliw:
        register_value = sign_extend_word(
            static_cast<std::uint32_t>(rs1) <<
            (decoded.immediate & 0x1FU));
        break;
    case InstructionKind::Srliw:
        register_value = sign_extend_word(
            static_cast<std::uint32_t>(rs1) >>
            (decoded.immediate & 0x1FU));
        break;
    case InstructionKind::Sraiw:
        register_value = sign_extend_word(
            static_cast<std::uint32_t>(
                as_signed_word(rs1) >>
                (decoded.immediate & 0x1FU)));
        break;
    case InstructionKind::Add:
        register_value = rs1 + rs2;
        break;
    case InstructionKind::Sub:
        register_value = rs1 - rs2;
        break;
    case InstructionKind::Sll:
        register_value = rs1 << (rs2 & 0x3FU);
        break;
    case InstructionKind::Slt:
        register_value = as_signed(rs1) < as_signed(rs2) ? 1U : 0U;
        break;
    case InstructionKind::Sltu:
        register_value = rs1 < rs2 ? 1U : 0U;
        break;
    case InstructionKind::Xor:
        register_value = rs1 ^ rs2;
        break;
    case InstructionKind::Srl:
        register_value = rs1 >> (rs2 & 0x3FU);
        break;
    case InstructionKind::Sra:
        register_value =
            std::bit_cast<std::uint64_t>(
                as_signed(rs1) >> (rs2 & 0x3FU));
        break;
    case InstructionKind::Or:
        register_value = rs1 | rs2;
        break;
    case InstructionKind::And:
        register_value = rs1 & rs2;
        break;
    case InstructionKind::Addw:
        register_value = sign_extend_word(rs1 + rs2);
        break;
    case InstructionKind::Subw:
        register_value = sign_extend_word(rs1 - rs2);
        break;
    case InstructionKind::Sllw:
        register_value = sign_extend_word(
            static_cast<std::uint32_t>(rs1) << (rs2 & 0x1FU));
        break;
    case InstructionKind::Srlw:
        register_value = sign_extend_word(
            static_cast<std::uint32_t>(rs1) >> (rs2 & 0x1FU));
        break;
    case InstructionKind::Sraw:
        register_value = sign_extend_word(
            static_cast<std::uint32_t>(
                as_signed_word(rs1) >> (rs2 & 0x1FU)));
        break;
    case InstructionKind::Lb:
    case InstructionKind::Lh:
    case InstructionKind::Lw:
    case InstructionKind::Ld:
    case InstructionKind::Lbu:
    case InstructionKind::Lhu:
    case InstructionKind::Lwu: {
        rv::AccessWidth width = rv::AccessWidth::Byte;
        unsigned int value_width = 8U;
        bool signed_load = true;
        switch (decoded.kind) {
        case InstructionKind::Lh:
            width = rv::AccessWidth::HalfWord;
            value_width = 16U;
            break;
        case InstructionKind::Lw:
            width = rv::AccessWidth::Word;
            value_width = 32U;
            break;
        case InstructionKind::Ld:
            width = rv::AccessWidth::DoubleWord;
            value_width = 64U;
            break;
        case InstructionKind::Lbu:
            signed_load = false;
            break;
        case InstructionKind::Lhu:
            width = rv::AccessWidth::HalfWord;
            value_width = 16U;
            signed_load = false;
            break;
        case InstructionKind::Lwu:
            width = rv::AccessWidth::Word;
            value_width = 32U;
            signed_load = false;
            break;
        default:
            break;
        }
        const std::uint64_t address = rs1 + decoded.immediate;
        if (!aligned(address, width)) {
            return failure(
                StepStatus::LoadAddressMisaligned,
                pc,
                instruction,
                address,
                rv::BusFault::Misaligned);
        }
        const rv::ReadResult loaded =
            bus_->read(address, width, rv::AccessKind::Load);
        if (!loaded.ok()) {
            return failure(
                StepStatus::LoadAccessFault,
                pc,
                instruction,
                address,
                loaded.fault);
        }
        register_value =
            signed_load ? sign_extend(loaded.value, value_width)
                        : loaded.value;
        break;
    }
    case InstructionKind::Sb:
    case InstructionKind::Sh:
    case InstructionKind::Sw:
    case InstructionKind::Sd: {
        rv::AccessWidth width = rv::AccessWidth::Byte;
        if (decoded.kind == InstructionKind::Sh) {
            width = rv::AccessWidth::HalfWord;
        } else if (decoded.kind == InstructionKind::Sw) {
            width = rv::AccessWidth::Word;
        } else if (decoded.kind == InstructionKind::Sd) {
            width = rv::AccessWidth::DoubleWord;
        }
        const std::uint64_t address = rs1 + decoded.immediate;
        if (!aligned(address, width)) {
            return failure(
                StepStatus::StoreAddressMisaligned,
                pc,
                instruction,
                address,
                rv::BusFault::Misaligned);
        }
        const rv::BusFault fault = bus_->write(
            address,
            width,
            rs2,
            rv::AccessKind::Store);
        if (fault != rv::BusFault::None) {
            return failure(
                StepStatus::StoreAccessFault,
                pc,
                instruction,
                address,
                fault);
        }
        break;
    }
    case InstructionKind::Fence:
    case InstructionKind::FenceI:
        break;
    case InstructionKind::Ecall:
        return failure(
            StepStatus::EnvironmentCall,
            pc,
            instruction,
            0);
    case InstructionKind::Ebreak:
        return failure(
            StepStatus::Breakpoint,
            pc,
            instruction,
            pc);
    case InstructionKind::Illegal:
        return failure(
            StepStatus::IllegalInstruction,
            pc,
            instruction,
            instruction);
    }

    if ((next_pc & 0x3U) != 0U) {
        return failure(
            StepStatus::InstructionAddressMisaligned,
            pc,
            instruction,
            next_pc,
            rv::BusFault::Misaligned);
    }

    RegisterCommit register_write;
    if (register_value.has_value() && decoded.rd != 0U) {
        state_.registers[decoded.rd] = *register_value;
        register_write = {
            .enabled = true,
            .index = decoded.rd,
            .value = *register_value,
        };
    }
    state_.registers[0] = 0;
    state_.pc = next_pc;
    ++state_.instructions_retired;
    return {
        .status = StepStatus::Retired,
        .pc = pc,
        .instruction = instruction,
        .trap_value = 0,
        .bus_fault = rv::BusFault::None,
        .register_write = register_write,
    };
}

const CpuSnapshot& Core::snapshot() const noexcept
{
    return state_;
}

} // namespace rv64
