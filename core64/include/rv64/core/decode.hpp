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
    Flw,
    Fld,
    Sb,
    Sh,
    Sw,
    Sd,
    Fsw,
    Fsd,
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
    LrD,
    ScD,
    AmoSwapD,
    AmoAddD,
    AmoXorD,
    AmoAndD,
    AmoOrD,
    AmoMinD,
    AmoMaxD,
    AmoMinuD,
    AmoMaxuD,
    FmvXW,
    FmvWX,
    FmvXD,
    FmvDX,
    FaddS,
    FsubS,
    FmulS,
    FdivS,
    FsqrtS,
    FmaddS,
    FmsubS,
    FnmsubS,
    FnmaddS,
    FaddD,
    FsubD,
    FmulD,
    FdivD,
    FsqrtD,
    FmaddD,
    FmsubD,
    FnmsubD,
    FnmaddD,
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

struct DecodedInstruction {
    InstructionKind kind{InstructionKind::Illegal};
    std::uint32_t raw{};
    std::uint8_t rd{};
    std::uint8_t rs1{};
    std::uint8_t rs2{};
    std::uint8_t rs3{};
    std::uint8_t rounding_mode{};
    std::uint64_t immediate{};
    bool acquire{};
    bool release{};
    std::uint16_t csr{};
    std::uint8_t length{4};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return kind != InstructionKind::Illegal;
    }
};

[[nodiscard]] DecodedInstruction decode_instruction(
    std::uint32_t instruction) noexcept;

[[nodiscard]] DecodedInstruction decode_compressed_instruction(
    std::uint16_t instruction) noexcept;

} // namespace rv64
