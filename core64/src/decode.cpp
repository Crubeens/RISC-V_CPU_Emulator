#include "rv64/core/decode.hpp"

namespace rv64 {

namespace {

[[nodiscard]] constexpr std::uint32_t bits(
    std::uint32_t value,
    unsigned int high,
    unsigned int low) noexcept
{
    const unsigned int width = high - low + 1U;
    const std::uint32_t mask =
        width == 32U
            ? 0xFFFFFFFFU
            : (std::uint32_t{1} << width) - 1U;
    return (value >> low) & mask;
}

[[nodiscard]] constexpr std::uint64_t sign_extend(
    std::uint32_t value,
    unsigned int width) noexcept
{
    const std::uint64_t sign =
        std::uint64_t{1} << (width - 1U);
    const std::uint64_t masked =
        static_cast<std::uint64_t>(value) &
        ((std::uint64_t{1} << width) - 1U);
    return (masked ^ sign) - sign;
}

[[nodiscard]] constexpr std::uint64_t i_immediate(
    std::uint32_t instruction) noexcept
{
    return sign_extend(bits(instruction, 31U, 20U), 12U);
}

[[nodiscard]] constexpr std::uint64_t s_immediate(
    std::uint32_t instruction) noexcept
{
    return sign_extend(
        (bits(instruction, 31U, 25U) << 5U) |
            bits(instruction, 11U, 7U),
        12U);
}

[[nodiscard]] constexpr std::uint64_t b_immediate(
    std::uint32_t instruction) noexcept
{
    return sign_extend(
        (bits(instruction, 31U, 31U) << 12U) |
            (bits(instruction, 7U, 7U) << 11U) |
            (bits(instruction, 30U, 25U) << 5U) |
            (bits(instruction, 11U, 8U) << 1U),
        13U);
}

[[nodiscard]] constexpr std::uint64_t u_immediate(
    std::uint32_t instruction) noexcept
{
    return sign_extend(instruction & 0xFFFFF000U, 32U);
}

[[nodiscard]] constexpr std::uint64_t j_immediate(
    std::uint32_t instruction) noexcept
{
    return sign_extend(
        (bits(instruction, 31U, 31U) << 20U) |
            (bits(instruction, 19U, 12U) << 12U) |
            (bits(instruction, 20U, 20U) << 11U) |
            (bits(instruction, 30U, 21U) << 1U),
        21U);
}

[[nodiscard]] constexpr DecodedInstruction make(
    std::uint32_t instruction,
    InstructionKind kind,
    std::uint64_t immediate = 0) noexcept
{
    return {
        .kind = kind,
        .raw = instruction,
        .rd = static_cast<std::uint8_t>(bits(instruction, 11U, 7U)),
        .rs1 = static_cast<std::uint8_t>(bits(instruction, 19U, 15U)),
        .rs2 = static_cast<std::uint8_t>(bits(instruction, 24U, 20U)),
        .immediate = immediate,
    };
}

} // namespace

DecodedInstruction decode_instruction(
    std::uint32_t instruction) noexcept
{
    const std::uint32_t opcode = bits(instruction, 6U, 0U);
    const std::uint32_t funct3 = bits(instruction, 14U, 12U);
    const std::uint32_t funct7 = bits(instruction, 31U, 25U);

    switch (opcode) {
    case 0x37U:
        return make(instruction, InstructionKind::Lui, u_immediate(instruction));
    case 0x17U:
        return make(
            instruction,
            InstructionKind::Auipc,
            u_immediate(instruction));
    case 0x6FU:
        return make(
            instruction,
            InstructionKind::Jal,
            j_immediate(instruction));
    case 0x67U:
        return funct3 == 0U
                   ? make(
                         instruction,
                         InstructionKind::Jalr,
                         i_immediate(instruction))
                   : make(instruction, InstructionKind::Illegal);
    case 0x63U: {
        InstructionKind kind = InstructionKind::Illegal;
        switch (funct3) {
        case 0U:
            kind = InstructionKind::Beq;
            break;
        case 1U:
            kind = InstructionKind::Bne;
            break;
        case 4U:
            kind = InstructionKind::Blt;
            break;
        case 5U:
            kind = InstructionKind::Bge;
            break;
        case 6U:
            kind = InstructionKind::Bltu;
            break;
        case 7U:
            kind = InstructionKind::Bgeu;
            break;
        default:
            break;
        }
        return make(instruction, kind, b_immediate(instruction));
    }
    case 0x03U: {
        InstructionKind kind = InstructionKind::Illegal;
        switch (funct3) {
        case 0U:
            kind = InstructionKind::Lb;
            break;
        case 1U:
            kind = InstructionKind::Lh;
            break;
        case 2U:
            kind = InstructionKind::Lw;
            break;
        case 3U:
            kind = InstructionKind::Ld;
            break;
        case 4U:
            kind = InstructionKind::Lbu;
            break;
        case 5U:
            kind = InstructionKind::Lhu;
            break;
        case 6U:
            kind = InstructionKind::Lwu;
            break;
        default:
            break;
        }
        return make(instruction, kind, i_immediate(instruction));
    }
    case 0x23U: {
        InstructionKind kind = InstructionKind::Illegal;
        switch (funct3) {
        case 0U:
            kind = InstructionKind::Sb;
            break;
        case 1U:
            kind = InstructionKind::Sh;
            break;
        case 2U:
            kind = InstructionKind::Sw;
            break;
        case 3U:
            kind = InstructionKind::Sd;
            break;
        default:
            break;
        }
        return make(instruction, kind, s_immediate(instruction));
    }
    case 0x13U:
        switch (funct3) {
        case 0U:
            return make(
                instruction,
                InstructionKind::Addi,
                i_immediate(instruction));
        case 2U:
            return make(
                instruction,
                InstructionKind::Slti,
                i_immediate(instruction));
        case 3U:
            return make(
                instruction,
                InstructionKind::Sltiu,
                i_immediate(instruction));
        case 4U:
            return make(
                instruction,
                InstructionKind::Xori,
                i_immediate(instruction));
        case 6U:
            return make(
                instruction,
                InstructionKind::Ori,
                i_immediate(instruction));
        case 7U:
            return make(
                instruction,
                InstructionKind::Andi,
                i_immediate(instruction));
        case 1U:
            return bits(instruction, 31U, 26U) == 0U
                       ? make(
                             instruction,
                             InstructionKind::Slli,
                             bits(instruction, 25U, 20U))
                       : make(instruction, InstructionKind::Illegal);
        case 5U:
            if (bits(instruction, 31U, 26U) == 0U) {
                return make(
                    instruction,
                    InstructionKind::Srli,
                    bits(instruction, 25U, 20U));
            }
            if (bits(instruction, 31U, 26U) == 0x10U) {
                return make(
                    instruction,
                    InstructionKind::Srai,
                    bits(instruction, 25U, 20U));
            }
            return make(instruction, InstructionKind::Illegal);
        default:
            return make(instruction, InstructionKind::Illegal);
        }
    case 0x1BU:
        if (funct3 == 0U) {
            return make(
                instruction,
                InstructionKind::Addiw,
                i_immediate(instruction));
        }
        if (funct3 == 1U && funct7 == 0U) {
            return make(
                instruction,
                InstructionKind::Slliw,
                bits(instruction, 24U, 20U));
        }
        if (funct3 == 5U && funct7 == 0U) {
            return make(
                instruction,
                InstructionKind::Srliw,
                bits(instruction, 24U, 20U));
        }
        if (funct3 == 5U && funct7 == 0x20U) {
            return make(
                instruction,
                InstructionKind::Sraiw,
                bits(instruction, 24U, 20U));
        }
        return make(instruction, InstructionKind::Illegal);
    case 0x33U: {
        InstructionKind kind = InstructionKind::Illegal;
        if (funct7 == 0U) {
            constexpr InstructionKind kinds[]{
                InstructionKind::Add,
                InstructionKind::Sll,
                InstructionKind::Slt,
                InstructionKind::Sltu,
                InstructionKind::Xor,
                InstructionKind::Srl,
                InstructionKind::Or,
                InstructionKind::And,
            };
            kind = kinds[funct3];
        } else if (funct7 == 0x20U) {
            if (funct3 == 0U) {
                kind = InstructionKind::Sub;
            } else if (funct3 == 5U) {
                kind = InstructionKind::Sra;
            }
        }
        return make(instruction, kind);
    }
    case 0x3BU: {
        InstructionKind kind = InstructionKind::Illegal;
        if (funct7 == 0U) {
            switch (funct3) {
            case 0U:
                kind = InstructionKind::Addw;
                break;
            case 1U:
                kind = InstructionKind::Sllw;
                break;
            case 5U:
                kind = InstructionKind::Srlw;
                break;
            default:
                break;
            }
        } else if (funct7 == 0x20U) {
            if (funct3 == 0U) {
                kind = InstructionKind::Subw;
            } else if (funct3 == 5U) {
                kind = InstructionKind::Sraw;
            }
        }
        return make(instruction, kind);
    }
    case 0x0FU:
        if (funct3 == 0U) {
            return make(instruction, InstructionKind::Fence);
        }
        if (funct3 == 1U && bits(instruction, 31U, 20U) == 0U &&
            bits(instruction, 19U, 15U) == 0U &&
            bits(instruction, 11U, 7U) == 0U) {
            return make(instruction, InstructionKind::FenceI);
        }
        return make(instruction, InstructionKind::Illegal);
    case 0x73U:
        if (instruction == 0x00000073U) {
            return make(instruction, InstructionKind::Ecall);
        }
        if (instruction == 0x00100073U) {
            return make(instruction, InstructionKind::Ebreak);
        }
        return make(instruction, InstructionKind::Illegal);
    default:
        return make(instruction, InstructionKind::Illegal);
    }
}

} // namespace rv64
