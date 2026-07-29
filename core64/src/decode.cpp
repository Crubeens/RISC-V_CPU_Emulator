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

[[nodiscard]] constexpr DecodedInstruction make_csr(
    std::uint32_t instruction,
    InstructionKind kind,
    bool immediate) noexcept
{
    DecodedInstruction decoded = make(
        instruction,
        kind,
        immediate ? bits(instruction, 19U, 15U) : 0U);
    decoded.csr = static_cast<std::uint16_t>(
        bits(instruction, 31U, 20U));
    return decoded;
}

[[nodiscard]] constexpr InstructionKind atomic_kind(
    std::uint32_t funct5,
    bool doubleword) noexcept
{
    switch (funct5) {
    case 0x02U:
        return doubleword ? InstructionKind::LrD : InstructionKind::LrW;
    case 0x03U:
        return doubleword ? InstructionKind::ScD : InstructionKind::ScW;
    case 0x01U:
        return doubleword ? InstructionKind::AmoSwapD
                          : InstructionKind::AmoSwapW;
    case 0x00U:
        return doubleword ? InstructionKind::AmoAddD
                          : InstructionKind::AmoAddW;
    case 0x04U:
        return doubleword ? InstructionKind::AmoXorD
                          : InstructionKind::AmoXorW;
    case 0x0CU:
        return doubleword ? InstructionKind::AmoAndD
                          : InstructionKind::AmoAndW;
    case 0x08U:
        return doubleword ? InstructionKind::AmoOrD
                          : InstructionKind::AmoOrW;
    case 0x10U:
        return doubleword ? InstructionKind::AmoMinD
                          : InstructionKind::AmoMinW;
    case 0x14U:
        return doubleword ? InstructionKind::AmoMaxD
                          : InstructionKind::AmoMaxW;
    case 0x18U:
        return doubleword ? InstructionKind::AmoMinuD
                          : InstructionKind::AmoMinuW;
    case 0x1CU:
        return doubleword ? InstructionKind::AmoMaxuD
                          : InstructionKind::AmoMaxuW;
    default:
        return InstructionKind::Illegal;
    }
}

[[nodiscard]] constexpr std::uint8_t compressed_register(
    std::uint32_t value) noexcept
{
    return static_cast<std::uint8_t>(8U + value);
}

[[nodiscard]] constexpr DecodedInstruction make_compressed(
    std::uint16_t instruction,
    InstructionKind kind,
    std::uint8_t rd = 0,
    std::uint8_t rs1 = 0,
    std::uint8_t rs2 = 0,
    std::uint64_t immediate = 0) noexcept
{
    return {
        .kind = kind,
        .raw = instruction,
        .rd = rd,
        .rs1 = rs1,
        .rs2 = rs2,
        .immediate = immediate,
        .length = 2,
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
        } else if (funct7 == 1U) {
            constexpr InstructionKind kinds[]{
                InstructionKind::Mul,
                InstructionKind::Mulh,
                InstructionKind::Mulhsu,
                InstructionKind::Mulhu,
                InstructionKind::Div,
                InstructionKind::Divu,
                InstructionKind::Rem,
                InstructionKind::Remu,
            };
            kind = kinds[funct3];
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
        } else if (funct7 == 1U) {
            switch (funct3) {
            case 0U:
                kind = InstructionKind::Mulw;
                break;
            case 4U:
                kind = InstructionKind::Divw;
                break;
            case 5U:
                kind = InstructionKind::Divuw;
                break;
            case 6U:
                kind = InstructionKind::Remw;
                break;
            case 7U:
                kind = InstructionKind::Remuw;
                break;
            default:
                break;
            }
        }
        return make(instruction, kind);
    }
    case 0x2FU: {
        if (funct3 != 2U && funct3 != 3U) {
            return make(instruction, InstructionKind::Illegal);
        }
        const bool doubleword = funct3 == 3U;
        const std::uint32_t funct5 = bits(instruction, 31U, 27U);
        const InstructionKind kind = atomic_kind(funct5, doubleword);
        if ((kind == InstructionKind::LrW ||
             kind == InstructionKind::LrD) &&
            bits(instruction, 24U, 20U) != 0U) {
            return make(instruction, InstructionKind::Illegal);
        }
        DecodedInstruction decoded = make(instruction, kind);
        decoded.acquire = bits(instruction, 26U, 26U) != 0U;
        decoded.release = bits(instruction, 25U, 25U) != 0U;
        return decoded;
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
        if (instruction == 0x30200073U) {
            return make(instruction, InstructionKind::Mret);
        }
        if (instruction == 0x10200073U) {
            return make(instruction, InstructionKind::Sret);
        }
        if (instruction == 0x10500073U) {
            return make(instruction, InstructionKind::Wfi);
        }
        if ((instruction & 0xFE007FFFU) == 0x12000073U) {
            return make(instruction, InstructionKind::SfenceVma);
        }
        switch (funct3) {
        case 1U:
            return make_csr(
                instruction,
                InstructionKind::Csrrw,
                false);
        case 2U:
            return make_csr(
                instruction,
                InstructionKind::Csrrs,
                false);
        case 3U:
            return make_csr(
                instruction,
                InstructionKind::Csrrc,
                false);
        case 5U:
            return make_csr(
                instruction,
                InstructionKind::Csrrwi,
                true);
        case 6U:
            return make_csr(
                instruction,
                InstructionKind::Csrrsi,
                true);
        case 7U:
            return make_csr(
                instruction,
                InstructionKind::Csrrci,
                true);
        default:
            break;
        }
        return make(instruction, InstructionKind::Illegal);
    default:
        return make(instruction, InstructionKind::Illegal);
    }
}

DecodedInstruction decode_compressed_instruction(
    std::uint16_t instruction) noexcept
{
    const std::uint32_t raw = instruction;
    const DecodedInstruction illegal = make_compressed(
        instruction,
        InstructionKind::Illegal);
    const std::uint32_t quadrant = bits(raw, 1U, 0U);
    const std::uint32_t funct3 = bits(raw, 15U, 13U);
    if (quadrant == 3U) {
        return illegal;
    }

    if (quadrant == 0U) {
        switch (funct3) {
        case 0U: {
            const std::uint64_t immediate =
                (bits(raw, 6U, 6U) << 2U) |
                (bits(raw, 5U, 5U) << 3U) |
                (bits(raw, 12U, 11U) << 4U) |
                (bits(raw, 10U, 7U) << 6U);
            if (immediate == 0U) {
                return illegal;
            }
            return make_compressed(
                instruction,
                InstructionKind::Addi,
                compressed_register(bits(raw, 4U, 2U)),
                2U,
                0U,
                immediate);
        }
        case 2U:
        case 3U: {
            const bool doubleword = funct3 == 3U;
            const std::uint64_t immediate =
                doubleword
                    ? ((bits(raw, 12U, 10U) << 3U) |
                       (bits(raw, 6U, 5U) << 6U))
                    : ((bits(raw, 6U, 6U) << 2U) |
                       (bits(raw, 12U, 10U) << 3U) |
                       (bits(raw, 5U, 5U) << 6U));
            return make_compressed(
                instruction,
                doubleword ? InstructionKind::Ld : InstructionKind::Lw,
                compressed_register(bits(raw, 4U, 2U)),
                compressed_register(bits(raw, 9U, 7U)),
                0U,
                immediate);
        }
        case 6U:
        case 7U: {
            const bool doubleword = funct3 == 7U;
            const std::uint64_t immediate =
                doubleword
                    ? ((bits(raw, 12U, 10U) << 3U) |
                       (bits(raw, 6U, 5U) << 6U))
                    : ((bits(raw, 6U, 6U) << 2U) |
                       (bits(raw, 12U, 10U) << 3U) |
                       (bits(raw, 5U, 5U) << 6U));
            return make_compressed(
                instruction,
                doubleword ? InstructionKind::Sd : InstructionKind::Sw,
                0U,
                compressed_register(bits(raw, 9U, 7U)),
                compressed_register(bits(raw, 4U, 2U)),
                immediate);
        }
        default:
            return illegal;
        }
    }

    if (quadrant == 1U) {
        const std::uint64_t immediate6 = sign_extend(
            (bits(raw, 12U, 12U) << 5U) |
                bits(raw, 6U, 2U),
            6U);
        switch (funct3) {
        case 0U: {
            const auto rd = static_cast<std::uint8_t>(
                bits(raw, 11U, 7U));
            return make_compressed(
                instruction,
                InstructionKind::Addi,
                rd,
                rd,
                0U,
                immediate6);
        }
        case 1U: {
            const auto rd = static_cast<std::uint8_t>(
                bits(raw, 11U, 7U));
            if (rd == 0U) {
                return illegal;
            }
            return make_compressed(
                instruction,
                InstructionKind::Addiw,
                rd,
                rd,
                0U,
                immediate6);
        }
        case 2U:
            return make_compressed(
                instruction,
                InstructionKind::Addi,
                static_cast<std::uint8_t>(bits(raw, 11U, 7U)),
                0U,
                0U,
                immediate6);
        case 3U: {
            const auto rd = static_cast<std::uint8_t>(
                bits(raw, 11U, 7U));
            if (rd == 2U) {
                const std::uint64_t immediate = sign_extend(
                    (bits(raw, 6U, 6U) << 4U) |
                        (bits(raw, 2U, 2U) << 5U) |
                        (bits(raw, 5U, 5U) << 6U) |
                        (bits(raw, 4U, 3U) << 7U) |
                        (bits(raw, 12U, 12U) << 9U),
                    10U);
                if (immediate == 0U) {
                    return illegal;
                }
                return make_compressed(
                    instruction,
                    InstructionKind::Addi,
                    2U,
                    2U,
                    0U,
                    immediate);
            }
            if (rd == 0U || immediate6 == 0U) {
                return illegal;
            }
            return make_compressed(
                instruction,
                InstructionKind::Lui,
                rd,
                0U,
                0U,
                immediate6 << 12U);
        }
        case 4U: {
            const std::uint32_t operation = bits(raw, 11U, 10U);
            const std::uint8_t rd =
                compressed_register(bits(raw, 9U, 7U));
            if (operation == 0U || operation == 1U) {
                const std::uint64_t shift =
                    (bits(raw, 12U, 12U) << 5U) |
                    bits(raw, 6U, 2U);
                return make_compressed(
                    instruction,
                    operation == 0U ? InstructionKind::Srli
                                    : InstructionKind::Srai,
                    rd,
                    rd,
                    0U,
                    shift);
            }
            if (operation == 2U) {
                return make_compressed(
                    instruction,
                    InstructionKind::Andi,
                    rd,
                    rd,
                    0U,
                    immediate6);
            }

            const std::uint8_t rs2 =
                compressed_register(bits(raw, 4U, 2U));
            const std::uint32_t subop = bits(raw, 6U, 5U);
            if (bits(raw, 12U, 12U) == 0U) {
                constexpr InstructionKind kinds[]{
                    InstructionKind::Sub,
                    InstructionKind::Xor,
                    InstructionKind::Or,
                    InstructionKind::And,
                };
                return make_compressed(
                    instruction,
                    kinds[subop],
                    rd,
                    rd,
                    rs2);
            }
            if (subop == 0U || subop == 1U) {
                return make_compressed(
                    instruction,
                    subop == 0U ? InstructionKind::Subw
                                : InstructionKind::Addw,
                    rd,
                    rd,
                    rs2);
            }
            return illegal;
        }
        case 5U: {
            const std::uint64_t immediate = sign_extend(
                (bits(raw, 5U, 3U) << 1U) |
                    (bits(raw, 11U, 11U) << 4U) |
                    (bits(raw, 2U, 2U) << 5U) |
                    (bits(raw, 7U, 7U) << 6U) |
                    (bits(raw, 6U, 6U) << 7U) |
                    (bits(raw, 10U, 9U) << 8U) |
                    (bits(raw, 8U, 8U) << 10U) |
                    (bits(raw, 12U, 12U) << 11U),
                12U);
            return make_compressed(
                instruction,
                InstructionKind::Jal,
                0U,
                0U,
                0U,
                immediate);
        }
        case 6U:
        case 7U: {
            const std::uint64_t immediate = sign_extend(
                (bits(raw, 4U, 3U) << 1U) |
                    (bits(raw, 11U, 10U) << 3U) |
                    (bits(raw, 2U, 2U) << 5U) |
                    (bits(raw, 6U, 5U) << 6U) |
                    (bits(raw, 12U, 12U) << 8U),
                9U);
            return make_compressed(
                instruction,
                funct3 == 6U ? InstructionKind::Beq
                             : InstructionKind::Bne,
                0U,
                compressed_register(bits(raw, 9U, 7U)),
                0U,
                immediate);
        }
        default:
            return illegal;
        }
    }

    switch (funct3) {
    case 0U: {
        const auto rd = static_cast<std::uint8_t>(
            bits(raw, 11U, 7U));
        const std::uint64_t shift =
            (bits(raw, 12U, 12U) << 5U) |
            bits(raw, 6U, 2U);
        return make_compressed(
            instruction,
            InstructionKind::Slli,
            rd,
            rd,
            0U,
            shift);
    }
    case 2U:
    case 3U: {
        const bool doubleword = funct3 == 3U;
        const auto rd = static_cast<std::uint8_t>(
            bits(raw, 11U, 7U));
        if (rd == 0U) {
            return illegal;
        }
        const std::uint64_t immediate =
            doubleword
                ? ((bits(raw, 6U, 5U) << 3U) |
                   (bits(raw, 12U, 12U) << 5U) |
                   (bits(raw, 4U, 2U) << 6U))
                : ((bits(raw, 6U, 4U) << 2U) |
                   (bits(raw, 12U, 12U) << 5U) |
                   (bits(raw, 3U, 2U) << 6U));
        return make_compressed(
            instruction,
            doubleword ? InstructionKind::Ld : InstructionKind::Lw,
            rd,
            2U,
            0U,
            immediate);
    }
    case 4U: {
        const auto rd = static_cast<std::uint8_t>(
            bits(raw, 11U, 7U));
        const auto rs2 = static_cast<std::uint8_t>(
            bits(raw, 6U, 2U));
        const bool alternate = bits(raw, 12U, 12U) != 0U;
        if (!alternate) {
            if (rs2 == 0U) {
                if (rd == 0U) {
                    return illegal;
                }
                return make_compressed(
                    instruction,
                    InstructionKind::Jalr,
                    0U,
                    rd);
            }
            return make_compressed(
                instruction,
                InstructionKind::Add,
                rd,
                0U,
                rs2);
        }
        if (rs2 == 0U) {
            if (rd == 0U) {
                return make_compressed(
                    instruction,
                    InstructionKind::Ebreak);
            }
            return make_compressed(
                instruction,
                InstructionKind::Jalr,
                1U,
                rd);
        }
        return make_compressed(
            instruction,
            InstructionKind::Add,
            rd,
            rd,
            rs2);
    }
    case 6U:
    case 7U: {
        const bool doubleword = funct3 == 7U;
        const std::uint64_t immediate =
            doubleword
                ? ((bits(raw, 12U, 10U) << 3U) |
                   (bits(raw, 9U, 7U) << 6U))
                : ((bits(raw, 12U, 9U) << 2U) |
                   (bits(raw, 8U, 7U) << 6U));
        return make_compressed(
            instruction,
            doubleword ? InstructionKind::Sd : InstructionKind::Sw,
            0U,
            2U,
            static_cast<std::uint8_t>(bits(raw, 6U, 2U)),
            immediate);
    }
    default:
        return illegal;
    }
}

} // namespace rv64
