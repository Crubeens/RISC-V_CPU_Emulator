#include "rv32/core/decode.hpp"

#include <cassert>

namespace rv32 {

namespace {

constexpr std::uint32_t opcode_load = 0x03U;
constexpr std::uint32_t opcode_misc_mem = 0x0FU;
constexpr std::uint32_t opcode_op_imm = 0x13U;
constexpr std::uint32_t opcode_auipc = 0x17U;
constexpr std::uint32_t opcode_store = 0x23U;
constexpr std::uint32_t opcode_amo = 0x2FU;
constexpr std::uint32_t opcode_op = 0x33U;
constexpr std::uint32_t opcode_lui = 0x37U;
constexpr std::uint32_t opcode_branch = 0x63U;
constexpr std::uint32_t opcode_jalr = 0x67U;
constexpr std::uint32_t opcode_jal = 0x6FU;
constexpr std::uint32_t opcode_system = 0x73U;

constexpr std::uint32_t funct7_base = 0x00U;
constexpr std::uint32_t funct7_multiply_divide = 0x01U;
constexpr std::uint32_t funct7_alternate = 0x20U;

[[nodiscard]] constexpr DecodedInstruction make_decoded(
    std::uint32_t raw,
    InstructionKind kind,
    std::uint32_t rd = 0,
    std::uint32_t rs1 = 0,
    std::uint32_t rs2 = 0,
    std::uint32_t immediate = 0,
    bool acquire = false,
    bool release = false,
    std::uint16_t csr = 0) noexcept
{
    return {
        .kind = kind,
        .raw = raw,
        .rd = rd,
        .rs1 = rs1,
        .rs2 = rs2,
        .immediate = immediate,
        .acquire = acquire,
        .release = release,
        .csr = csr,
    };
}

} // namespace

std::uint32_t get_bits(
    std::uint32_t value,
    unsigned int high,
    unsigned int low) noexcept
{
    const bool valid_range = high < 32U && low <= high;
    assert(valid_range);
    if (!valid_range) {
        return 0;
    }

    const unsigned int width = high - low + 1U;
    if (width == 32U) {
        return value;
    }

    const std::uint32_t mask =
        (std::uint32_t{1} << width) - 1U;
    return (value >> low) & mask;
}

std::uint32_t sign_extend(
    std::uint32_t value,
    unsigned int width) noexcept
{
    const bool valid_width = width > 0U && width <= 32U;
    assert(valid_width);
    if (!valid_width) {
        return 0;
    }

    if (width == 32U) {
        return value;
    }

    const std::uint32_t value_mask =
        (std::uint32_t{1} << width) - 1U;
    const std::uint32_t masked_value = value & value_mask;
    const std::uint32_t sign_bit =
        std::uint32_t{1} << (width - 1U);

    if ((masked_value & sign_bit) != 0U) {
        return masked_value | ~value_mask;
    }
    return masked_value;
}

std::uint32_t decode_i_imm(
    std::uint32_t instruction) noexcept
{
    return sign_extend(get_bits(instruction, 31U, 20U), 12U);
}

std::uint32_t decode_s_imm(
    std::uint32_t instruction) noexcept
{
    const std::uint32_t immediate =
        (get_bits(instruction, 31U, 25U) << 5U) |
        get_bits(instruction, 11U, 7U);
    return sign_extend(immediate, 12U);
}

std::uint32_t decode_b_imm(
    std::uint32_t instruction) noexcept
{
    const std::uint32_t immediate =
        (get_bits(instruction, 31U, 31U) << 12U) |
        (get_bits(instruction, 7U, 7U) << 11U) |
        (get_bits(instruction, 30U, 25U) << 5U) |
        (get_bits(instruction, 11U, 8U) << 1U);
    return sign_extend(immediate, 13U);
}

std::uint32_t decode_u_imm(
    std::uint32_t instruction) noexcept
{
    return instruction & 0xFFFFF000U;
}

std::uint32_t decode_j_imm(
    std::uint32_t instruction) noexcept
{
    const std::uint32_t immediate =
        (get_bits(instruction, 31U, 31U) << 20U) |
        (get_bits(instruction, 19U, 12U) << 12U) |
        (get_bits(instruction, 20U, 20U) << 11U) |
        (get_bits(instruction, 30U, 21U) << 1U);
    return sign_extend(immediate, 21U);
}

InstructionFields extract_fields(
    std::uint32_t instruction) noexcept
{
    return {
        .opcode = get_bits(instruction, 6U, 0U),
        .rd = get_bits(instruction, 11U, 7U),
        .funct3 = get_bits(instruction, 14U, 12U),
        .rs1 = get_bits(instruction, 19U, 15U),
        .rs2 = get_bits(instruction, 24U, 20U),
        .funct7 = get_bits(instruction, 31U, 25U),
    };
}

DecodedInstruction decode_instruction(
    std::uint32_t instruction) noexcept
{
    const InstructionFields fields = extract_fields(instruction);
    const DecodedInstruction illegal =
        make_decoded(instruction, InstructionKind::Illegal);

    const auto make_r = [&](InstructionKind kind) noexcept {
        return make_decoded(
            instruction,
            kind,
            fields.rd,
            fields.rs1,
            fields.rs2);
    };
    const auto make_i =
        [&](InstructionKind kind, std::uint32_t immediate) noexcept {
            return make_decoded(
                instruction,
                kind,
                fields.rd,
                fields.rs1,
                0,
                immediate);
        };
    const auto make_s = [&](InstructionKind kind) noexcept {
        return make_decoded(
            instruction,
            kind,
            0,
            fields.rs1,
            fields.rs2,
            decode_s_imm(instruction));
    };
    const auto make_b = [&](InstructionKind kind) noexcept {
        return make_decoded(
            instruction,
            kind,
            0,
            fields.rs1,
            fields.rs2,
            decode_b_imm(instruction));
    };
    const auto make_a = [&](InstructionKind kind) noexcept {
        return make_decoded(
            instruction,
            kind,
            fields.rd,
            fields.rs1,
            fields.rs2,
            0,
            get_bits(instruction, 26U, 26U) != 0U,
            get_bits(instruction, 25U, 25U) != 0U);
    };
    const auto make_csr =
        [&](InstructionKind kind, bool immediate_form) noexcept {
            return make_decoded(
                instruction,
                kind,
                fields.rd,
                immediate_form ? 0U : fields.rs1,
                0,
                immediate_form ? fields.rs1 : 0U,
                false,
                false,
                static_cast<std::uint16_t>(
                    get_bits(instruction, 31U, 20U)));
        };

    switch (fields.opcode) {
    case opcode_lui:
        return make_decoded(
            instruction,
            InstructionKind::Lui,
            fields.rd,
            0,
            0,
            decode_u_imm(instruction));

    case opcode_auipc:
        return make_decoded(
            instruction,
            InstructionKind::Auipc,
            fields.rd,
            0,
            0,
            decode_u_imm(instruction));

    case opcode_jal:
        return make_decoded(
            instruction,
            InstructionKind::Jal,
            fields.rd,
            0,
            0,
            decode_j_imm(instruction));

    case opcode_jalr:
        if (fields.funct3 == 0x0U) {
            return make_i(
                InstructionKind::Jalr,
                decode_i_imm(instruction));
        }
        return illegal;

    case opcode_branch:
        switch (fields.funct3) {
        case 0x0U:
            return make_b(InstructionKind::Beq);
        case 0x1U:
            return make_b(InstructionKind::Bne);
        case 0x4U:
            return make_b(InstructionKind::Blt);
        case 0x5U:
            return make_b(InstructionKind::Bge);
        case 0x6U:
            return make_b(InstructionKind::Bltu);
        case 0x7U:
            return make_b(InstructionKind::Bgeu);
        default:
            return illegal;
        }

    case opcode_load:
        switch (fields.funct3) {
        case 0x0U:
            return make_i(
                InstructionKind::Lb,
                decode_i_imm(instruction));
        case 0x1U:
            return make_i(
                InstructionKind::Lh,
                decode_i_imm(instruction));
        case 0x2U:
            return make_i(
                InstructionKind::Lw,
                decode_i_imm(instruction));
        case 0x4U:
            return make_i(
                InstructionKind::Lbu,
                decode_i_imm(instruction));
        case 0x5U:
            return make_i(
                InstructionKind::Lhu,
                decode_i_imm(instruction));
        default:
            return illegal;
        }

    case opcode_store:
        switch (fields.funct3) {
        case 0x0U:
            return make_s(InstructionKind::Sb);
        case 0x1U:
            return make_s(InstructionKind::Sh);
        case 0x2U:
            return make_s(InstructionKind::Sw);
        default:
            return illegal;
        }

    case opcode_op_imm:
        switch (fields.funct3) {
        case 0x0U:
            return make_i(
                InstructionKind::Addi,
                decode_i_imm(instruction));
        case 0x2U:
            return make_i(
                InstructionKind::Slti,
                decode_i_imm(instruction));
        case 0x3U:
            return make_i(
                InstructionKind::Sltiu,
                decode_i_imm(instruction));
        case 0x4U:
            return make_i(
                InstructionKind::Xori,
                decode_i_imm(instruction));
        case 0x6U:
            return make_i(
                InstructionKind::Ori,
                decode_i_imm(instruction));
        case 0x7U:
            return make_i(
                InstructionKind::Andi,
                decode_i_imm(instruction));
        case 0x1U:
            if (fields.funct7 == funct7_base) {
                return make_i(InstructionKind::Slli, fields.rs2);
            }
            return illegal;
        case 0x5U:
            if (fields.funct7 == funct7_base) {
                return make_i(InstructionKind::Srli, fields.rs2);
            }
            if (fields.funct7 == funct7_alternate) {
                return make_i(InstructionKind::Srai, fields.rs2);
            }
            return illegal;
        default:
            return illegal;
        }

    case opcode_op:
        switch (fields.funct3) {
        case 0x0U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::Add);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Mul);
            }
            if (fields.funct7 == funct7_alternate) {
                return make_r(InstructionKind::Sub);
            }
            return illegal;
        case 0x1U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::Sll);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Mulh);
            }
            return illegal;
        case 0x2U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::Slt);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Mulhsu);
            }
            return illegal;
        case 0x3U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::Sltu);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Mulhu);
            }
            return illegal;
        case 0x4U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::Xor);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Div);
            }
            return illegal;
        case 0x5U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::Srl);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Divu);
            }
            if (fields.funct7 == funct7_alternate) {
                return make_r(InstructionKind::Sra);
            }
            return illegal;
        case 0x6U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::Or);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Rem);
            }
            return illegal;
        case 0x7U:
            if (fields.funct7 == funct7_base) {
                return make_r(InstructionKind::And);
            }
            if (fields.funct7 == funct7_multiply_divide) {
                return make_r(InstructionKind::Remu);
            }
            return illegal;
        default:
            return illegal;
        }

    case opcode_amo:
        if (fields.funct3 != 0x2U) {
            return illegal;
        }

        switch (get_bits(instruction, 31U, 27U)) {
        case 0x00U:
            return make_a(InstructionKind::AmoAddW);
        case 0x01U:
            return make_a(InstructionKind::AmoSwapW);
        case 0x02U:
            return fields.rs2 == 0U
                       ? make_a(InstructionKind::LrW)
                       : illegal;
        case 0x03U:
            return make_a(InstructionKind::ScW);
        case 0x04U:
            return make_a(InstructionKind::AmoXorW);
        case 0x08U:
            return make_a(InstructionKind::AmoOrW);
        case 0x0CU:
            return make_a(InstructionKind::AmoAndW);
        case 0x10U:
            return make_a(InstructionKind::AmoMinW);
        case 0x14U:
            return make_a(InstructionKind::AmoMaxW);
        case 0x18U:
            return make_a(InstructionKind::AmoMinuW);
        case 0x1CU:
            return make_a(InstructionKind::AmoMaxuW);
        default:
            return illegal;
        }

    case opcode_misc_mem:
        // RV32I requires implementations to ignore the reserved rd, rs1,
        // fm, predecessor, and successor configurations for FENCE.
        if (fields.funct3 == 0x0U) {
            return make_decoded(
                instruction,
                InstructionKind::Fence);
        }
        // Zifencei likewise requires the reserved immediate, rs1, and rd
        // fields to be ignored by base implementations.
        if (fields.funct3 == 0x1U) {
            return make_decoded(
                instruction,
                InstructionKind::FenceI);
        }
        return illegal;

    case opcode_system:
        if (instruction == 0x00000073U) {
            return make_decoded(
                instruction,
                InstructionKind::Ecall);
        }
        if (instruction == 0x00100073U) {
            return make_decoded(
                instruction,
                InstructionKind::Ebreak);
        }
        if (instruction == 0x30200073U) {
            return make_decoded(
                instruction,
                InstructionKind::Mret);
        }
        if (instruction == 0x10200073U) {
            return make_decoded(
                instruction,
                InstructionKind::Sret);
        }
        if (instruction == 0x10500073U) {
            return make_decoded(
                instruction,
                InstructionKind::Wfi);
        }
        if ((instruction & 0xFE007FFFU) == 0x12000073U) {
            return make_decoded(
                instruction,
                InstructionKind::SfenceVma,
                0,
                fields.rs1,
                fields.rs2);
        }
        switch (fields.funct3) {
        case 0x1U:
            return make_csr(InstructionKind::Csrrw, false);
        case 0x2U:
            return make_csr(InstructionKind::Csrrs, false);
        case 0x3U:
            return make_csr(InstructionKind::Csrrc, false);
        case 0x5U:
            return make_csr(InstructionKind::Csrrwi, true);
        case 0x6U:
            return make_csr(InstructionKind::Csrrsi, true);
        case 0x7U:
            return make_csr(InstructionKind::Csrrci, true);
        default:
            return illegal;
        }

    default:
        return illegal;
    }
}

} // namespace rv32
