#pragma once

#include <cstdint>

namespace rv32 {

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
    Lbu,
    Lhu,
    Sb,
    Sh,
    Sw,
    Addi,
    Slti,
    Sltiu,
    Xori,
    Ori,
    Andi,
    Slli,
    Srli,
    Srai,
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
    LrW,
    ScW,
    AmoSwapW,
    AmoAddW,
    AmoXorW,
    AmoAndW,
    AmoOrW,
    AmoMinW,
    AmoMaxW,
    AmoMinuW,
    AmoMaxuW,
    Fence,
    FenceI,
    Csrrw,
    Csrrs,
    Csrrc,
    Csrrwi,
    Csrrsi,
    Csrrci,
    Ecall,
    Ebreak,
    Mret,
    Sret,
    Wfi,
    SfenceVma,
};

struct InstructionFields {
    std::uint32_t opcode{};
    std::uint32_t rd{};
    std::uint32_t funct3{};
    std::uint32_t rs1{};
    std::uint32_t rs2{};
    std::uint32_t funct7{};
};

struct DecodedInstruction {
    InstructionKind kind{InstructionKind::Illegal};
    std::uint32_t raw{};
    std::uint32_t rd{};
    std::uint32_t rs1{};
    std::uint32_t rs2{};
    std::uint32_t immediate{};
    bool acquire{};
    bool release{};
    std::uint16_t csr{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return kind != InstructionKind::Illegal;
    }
};

[[nodiscard]] std::uint32_t get_bits(
    std::uint32_t value,
    unsigned int high,
    unsigned int low) noexcept;

[[nodiscard]] std::uint32_t sign_extend(
    std::uint32_t value,
    unsigned int width) noexcept;

[[nodiscard]] std::uint32_t decode_i_imm(
    std::uint32_t instruction) noexcept;

[[nodiscard]] std::uint32_t decode_s_imm(
    std::uint32_t instruction) noexcept;

[[nodiscard]] std::uint32_t decode_b_imm(
    std::uint32_t instruction) noexcept;

[[nodiscard]] std::uint32_t decode_u_imm(
    std::uint32_t instruction) noexcept;

[[nodiscard]] std::uint32_t decode_j_imm(
    std::uint32_t instruction) noexcept;

[[nodiscard]] InstructionFields extract_fields(
    std::uint32_t instruction) noexcept;

[[nodiscard]] DecodedInstruction decode_instruction(
    std::uint32_t instruction) noexcept;

} // namespace rv32
