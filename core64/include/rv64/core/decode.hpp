#pragma once

#include <cstdint>

namespace rv64 {

enum class InstructionKind : std::uint8_t {
    Illegal,
    Lui,
    Auipc,
    Jal,
    Jalr,
    Beq,
    Bne,
    Blt,
    Bge,
    Bltu,
    Bgeu,
    Lb,
    Lh,
    Lw,
    Ld,
    Lbu,
    Lhu,
    Lwu,
    Sb,
    Sh,
    Sw,
    Sd,
    Addi,
    Slti,
    Sltiu,
    Xori,
    Ori,
    Andi,
    Slli,
    Srli,
    Srai,
    Addiw,
    Slliw,
    Srliw,
    Sraiw,
    Add,
    Sub,
    Sll,
    Slt,
    Sltu,
    Xor,
    Srl,
    Sra,
    Or,
    And,
    Mul,
    Mulh,
    Mulhsu,
    Mulhu,
    Div,
    Divu,
    Rem,
    Remu,
    Addw,
    Subw,
    Sllw,
    Srlw,
    Sraw,
    Mulw,
    Divw,
    Divuw,
    Remw,
    Remuw,
    Fence,
    FenceI,
    Ecall,
    Ebreak,
};

struct DecodedInstruction {
    InstructionKind kind{InstructionKind::Illegal};
    std::uint32_t raw{};
    std::uint8_t rd{};
    std::uint8_t rs1{};
    std::uint8_t rs2{};
    std::uint64_t immediate{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return kind != InstructionKind::Illegal;
    }
};

[[nodiscard]] DecodedInstruction decode_instruction(
    std::uint32_t instruction) noexcept;

} // namespace rv64
