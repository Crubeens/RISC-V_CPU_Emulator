#include "rv64/core/core.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "rv64/core/csr.hpp"
#include "rv64/core/decode.hpp"
#include "rv64/core/interrupt.hpp"
#include "rv64/core/trap.hpp"

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

[[nodiscard]] constexpr std::uint64_t multiply_high_unsigned(
    std::uint64_t lhs,
    std::uint64_t rhs) noexcept
{
    const std::uint64_t lhs_low = static_cast<std::uint32_t>(lhs);
    const std::uint64_t lhs_high = lhs >> 32U;
    const std::uint64_t rhs_low = static_cast<std::uint32_t>(rhs);
    const std::uint64_t rhs_high = rhs >> 32U;

    const std::uint64_t low_product = lhs_low * rhs_low;
    const std::uint64_t cross1 = lhs_low * rhs_high;
    const std::uint64_t cross2 = lhs_high * rhs_low;
    const std::uint64_t high_product = lhs_high * rhs_high;
    const std::uint64_t carry =
        ((low_product >> 32U) +
         static_cast<std::uint32_t>(cross1) +
         static_cast<std::uint32_t>(cross2)) >>
        32U;
    return high_product + (cross1 >> 32U) +
           (cross2 >> 32U) + carry;
}

[[nodiscard]] constexpr std::uint64_t multiply_high_signed(
    std::uint64_t lhs,
    std::uint64_t rhs) noexcept
{
    std::uint64_t result = multiply_high_unsigned(lhs, rhs);
    if ((lhs >> 63U) != 0U) {
        result -= rhs;
    }
    if ((rhs >> 63U) != 0U) {
        result -= lhs;
    }
    return result;
}

[[nodiscard]] constexpr std::uint64_t multiply_high_signed_unsigned(
    std::uint64_t lhs,
    std::uint64_t rhs) noexcept
{
    std::uint64_t result = multiply_high_unsigned(lhs, rhs);
    if ((lhs >> 63U) != 0U) {
        result -= rhs;
    }
    return result;
}

[[nodiscard]] constexpr bool aligned(
    std::uint64_t address,
    rv::AccessWidth width) noexcept
{
    const std::uint64_t bytes = rv::width_bytes(width);
    return (address & (bytes - 1U)) == 0U;
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
    state_.hart_id = config.hart_id;
    state_.privilege = config.initial_privilege;
    state_.machine_csrs.mstatus = sanitize_mstatus(0);
    state_.registers[10] = config.hart_id;
    state_.registers[11] = config.boot_argument;
    sampled_irq_lines_ = {};
}

StepResult Core::step(const IrqLines& irq_lines)
{
    ++state_.cycle;
    state_.registers[0] = 0;
    sampled_irq_lines_ = irq_lines;
    sample_interrupt_lines(state_, irq_lines);

    if (state_.waiting_for_interrupt) {
        if (!interrupt_wake_requested(state_)) {
            return {
                .status = StepStatus::WaitingForInterrupt,
                .privilege = state_.privilege,
                .pc = state_.pc,
            };
        }
        state_.waiting_for_interrupt = false;
    }

    const InterruptSelection interrupt =
        select_pending_interrupt(state_);
    if (interrupt.pending) {
        const PrivilegeMode interrupted_privilege = state_.privilege;
        const Xlen interrupted_pc = state_.pc;
        take_interrupt_trap(
            state_,
            {
                .cause = interrupt.cause,
                .interrupted_pc = interrupted_pc,
            },
            interrupt.target);
        return {
            .status = StepStatus::TrapTaken,
            .privilege = interrupted_privilege,
            .pc = interrupted_pc,
        };
    }

    const std::uint64_t pc = state_.pc;
    const PrivilegeMode executing_privilege = state_.privilege;
    const auto trap =
        [&](ExceptionCause cause,
            std::uint32_t instruction,
            Xlen trap_value,
            rv::BusFault bus_fault = rv::BusFault::None) {
        static_cast<void>(take_trap(
            state_,
            {
                .cause = cause,
                .exception_pc = pc,
                .trap_value = trap_value,
            }));
        return StepResult{
            .status = StepStatus::TrapTaken,
            .privilege = executing_privilege,
            .pc = pc,
            .instruction = instruction,
            .trap_value = trap_value,
            .bus_fault = bus_fault,
        };
    };

    if ((pc & 0x1U) != 0U) {
        return trap(
            ExceptionCause::InstructionAddressMisaligned,
            0,
            pc,
            rv::BusFault::Misaligned);
    }

    const rv::ReadResult first = bus_->read(
        pc,
        rv::AccessWidth::HalfWord,
        rv::AccessKind::InstructionFetch);
    if (!first.ok()) {
        return trap(
            ExceptionCause::InstructionAccessFault,
            0,
            pc,
            first.fault);
    }

    std::uint32_t instruction =
        static_cast<std::uint16_t>(first.value);
    DecodedInstruction decoded;
    if ((instruction & 0x3U) != 0x3U) {
        decoded = decode_compressed_instruction(
            static_cast<std::uint16_t>(instruction));
    } else {
        const rv::ReadResult second = bus_->read(
            pc + 2U,
            rv::AccessWidth::HalfWord,
            rv::AccessKind::InstructionFetch);
        if (!second.ok()) {
            return trap(
                ExceptionCause::InstructionAccessFault,
                instruction,
                pc + 2U,
                second.fault);
        }
        instruction |=
            static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(second.value))
            << 16U;
        decoded = decode_instruction(instruction);
    }
    if (!decoded.valid()) {
        return trap(
            ExceptionCause::IllegalInstruction,
            instruction,
            instruction);
    }

    CsrFile csr_file(state_, *bus_);
    const std::uint64_t rs1 = state_.registers[decoded.rs1];
    const std::uint64_t rs2 = state_.registers[decoded.rs2];
    std::uint64_t next_pc = pc + decoded.length;
    std::optional<std::uint64_t> register_value;
    std::optional<std::pair<CsrAddress, Xlen>> csr_write;
    std::optional<CpuSnapshot> privileged_state;
    bool wait_for_interrupt = false;

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
    case InstructionKind::Mul:
        register_value = rs1 * rs2;
        break;
    case InstructionKind::Mulh:
        register_value = multiply_high_signed(rs1, rs2);
        break;
    case InstructionKind::Mulhsu:
        register_value = multiply_high_signed_unsigned(rs1, rs2);
        break;
    case InstructionKind::Mulhu:
        register_value = multiply_high_unsigned(rs1, rs2);
        break;
    case InstructionKind::Div:
        if (rs2 == 0U) {
            register_value = std::numeric_limits<std::uint64_t>::max();
        } else if (
            rs1 == (std::uint64_t{1} << 63U) &&
            rs2 == std::numeric_limits<std::uint64_t>::max()) {
            register_value = rs1;
        } else {
            register_value = std::bit_cast<std::uint64_t>(
                as_signed(rs1) / as_signed(rs2));
        }
        break;
    case InstructionKind::Divu:
        register_value =
            rs2 == 0U
                ? std::numeric_limits<std::uint64_t>::max()
                : rs1 / rs2;
        break;
    case InstructionKind::Rem:
        if (rs2 == 0U) {
            register_value = rs1;
        } else if (
            rs1 == (std::uint64_t{1} << 63U) &&
            rs2 == std::numeric_limits<std::uint64_t>::max()) {
            register_value = 0U;
        } else {
            register_value = std::bit_cast<std::uint64_t>(
                as_signed(rs1) % as_signed(rs2));
        }
        break;
    case InstructionKind::Remu:
        register_value = rs2 == 0U ? rs1 : rs1 % rs2;
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
    case InstructionKind::Mulw:
        register_value = sign_extend_word(
            static_cast<std::uint32_t>(rs1) *
            static_cast<std::uint32_t>(rs2));
        break;
    case InstructionKind::Divw: {
        const std::int32_t lhs = as_signed_word(rs1);
        const std::int32_t rhs = as_signed_word(rs2);
        std::int32_t result{};
        if (rhs == 0) {
            result = -1;
        } else if (
            lhs == std::numeric_limits<std::int32_t>::min() &&
            rhs == -1) {
            result = lhs;
        } else {
            result = lhs / rhs;
        }
        register_value = sign_extend_word(
            std::bit_cast<std::uint32_t>(result));
        break;
    }
    case InstructionKind::Divuw: {
        const std::uint32_t lhs = static_cast<std::uint32_t>(rs1);
        const std::uint32_t rhs = static_cast<std::uint32_t>(rs2);
        register_value = sign_extend_word(
            rhs == 0U ? std::numeric_limits<std::uint32_t>::max()
                      : lhs / rhs);
        break;
    }
    case InstructionKind::Remw: {
        const std::int32_t lhs = as_signed_word(rs1);
        const std::int32_t rhs = as_signed_word(rs2);
        std::int32_t result{};
        if (rhs == 0) {
            result = lhs;
        } else if (
            lhs == std::numeric_limits<std::int32_t>::min() &&
            rhs == -1) {
            result = 0;
        } else {
            result = lhs % rhs;
        }
        register_value = sign_extend_word(
            std::bit_cast<std::uint32_t>(result));
        break;
    }
    case InstructionKind::Remuw: {
        const std::uint32_t lhs = static_cast<std::uint32_t>(rs1);
        const std::uint32_t rhs = static_cast<std::uint32_t>(rs2);
        register_value = sign_extend_word(
            rhs == 0U ? lhs : lhs % rhs);
        break;
    }
    case InstructionKind::LrW:
    case InstructionKind::ScW:
    case InstructionKind::AmoSwapW:
    case InstructionKind::AmoAddW:
    case InstructionKind::AmoXorW:
    case InstructionKind::AmoAndW:
    case InstructionKind::AmoOrW:
    case InstructionKind::AmoMinW:
    case InstructionKind::AmoMaxW:
    case InstructionKind::AmoMinuW:
    case InstructionKind::AmoMaxuW:
    case InstructionKind::LrD:
    case InstructionKind::ScD:
    case InstructionKind::AmoSwapD:
    case InstructionKind::AmoAddD:
    case InstructionKind::AmoXorD:
    case InstructionKind::AmoAndD:
    case InstructionKind::AmoOrD:
    case InstructionKind::AmoMinD:
    case InstructionKind::AmoMaxD:
    case InstructionKind::AmoMinuD:
    case InstructionKind::AmoMaxuD: {
        const bool doubleword =
            ((instruction >> 12U) & 0x7U) == 3U;
        const rv::AccessWidth width =
            doubleword ? rv::AccessWidth::DoubleWord
                       : rv::AccessWidth::Word;
        const std::uint64_t address = rs1;
        const bool load_reserved =
            decoded.kind == InstructionKind::LrW ||
            decoded.kind == InstructionKind::LrD;
        const bool store_conditional =
            decoded.kind == InstructionKind::ScW ||
            decoded.kind == InstructionKind::ScD;
        if (!aligned(address, width)) {
            return trap(
                load_reserved
                    ? ExceptionCause::LoadAddressMisaligned
                    : ExceptionCause::StoreAddressMisaligned,
                instruction,
                address,
                rv::BusFault::Misaligned);
        }

        if (load_reserved) {
            const rv::ReadResult result =
                doubleword
                    ? bus_->load_reserved_doubleword(
                          static_cast<std::uint32_t>(state_.hart_id),
                          address)
                    : bus_->load_reserved_word(
                          static_cast<std::uint32_t>(state_.hart_id),
                          address);
            if (!result.ok()) {
                return trap(
                    ExceptionCause::LoadAccessFault,
                    instruction,
                    address,
                    result.fault);
            }
            register_value =
                doubleword ? result.value
                           : sign_extend_word(result.value);
            break;
        }

        if (store_conditional) {
            const rv::StoreConditionalResult result =
                doubleword
                    ? bus_->store_conditional_doubleword(
                          static_cast<std::uint32_t>(state_.hart_id),
                          address,
                          rs2)
                    : bus_->store_conditional_word(
                          static_cast<std::uint32_t>(state_.hart_id),
                          address,
                          static_cast<std::uint32_t>(rs2));
            if (!result.ok()) {
                return trap(
                    ExceptionCause::StoreAccessFault,
                    instruction,
                    address,
                    result.fault);
            }
            register_value = result.succeeded ? 0U : 1U;
            break;
        }

        rv::AmoOperation operation = rv::AmoOperation::Swap;
        switch (decoded.kind) {
        case InstructionKind::AmoAddW:
        case InstructionKind::AmoAddD:
            operation = rv::AmoOperation::Add;
            break;
        case InstructionKind::AmoXorW:
        case InstructionKind::AmoXorD:
            operation = rv::AmoOperation::Xor;
            break;
        case InstructionKind::AmoAndW:
        case InstructionKind::AmoAndD:
            operation = rv::AmoOperation::And;
            break;
        case InstructionKind::AmoOrW:
        case InstructionKind::AmoOrD:
            operation = rv::AmoOperation::Or;
            break;
        case InstructionKind::AmoMinW:
        case InstructionKind::AmoMinD:
            operation = rv::AmoOperation::Min;
            break;
        case InstructionKind::AmoMaxW:
        case InstructionKind::AmoMaxD:
            operation = rv::AmoOperation::Max;
            break;
        case InstructionKind::AmoMinuW:
        case InstructionKind::AmoMinuD:
            operation = rv::AmoOperation::MinUnsigned;
            break;
        case InstructionKind::AmoMaxuW:
        case InstructionKind::AmoMaxuD:
            operation = rv::AmoOperation::MaxUnsigned;
            break;
        default:
            break;
        }

        if (doubleword) {
            const rv::AtomicResult64 result =
                bus_->atomic_doubleword(
                    static_cast<std::uint32_t>(state_.hart_id),
                    address,
                    operation,
                    rs2);
            if (!result.ok()) {
                return trap(
                    ExceptionCause::StoreAccessFault,
                    instruction,
                    address,
                    result.fault);
            }
            register_value = result.original_value;
        } else {
            const rv::AtomicResult result = bus_->atomic_word(
                static_cast<std::uint32_t>(state_.hart_id),
                address,
                operation,
                static_cast<std::uint32_t>(rs2));
            if (!result.ok()) {
                return trap(
                    ExceptionCause::StoreAccessFault,
                    instruction,
                    address,
                    result.fault);
            }
            register_value = sign_extend_word(result.original_value);
        }
        break;
    }
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
            return trap(
                ExceptionCause::LoadAddressMisaligned,
                instruction,
                address,
                rv::BusFault::Misaligned);
        }
        const rv::ReadResult loaded =
            bus_->read(address, width, rv::AccessKind::Load);
        if (!loaded.ok()) {
            return trap(
                ExceptionCause::LoadAccessFault,
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
            return trap(
                ExceptionCause::StoreAddressMisaligned,
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
            return trap(
                ExceptionCause::StoreAccessFault,
                instruction,
                address,
                fault);
        }
        break;
    }
    case InstructionKind::Fence:
    case InstructionKind::FenceI:
        break;
    case InstructionKind::Csrrw:
    case InstructionKind::Csrrs:
    case InstructionKind::Csrrc:
    case InstructionKind::Csrrwi:
    case InstructionKind::Csrrsi:
    case InstructionKind::Csrrci: {
        const bool immediate =
            decoded.kind == InstructionKind::Csrrwi ||
            decoded.kind == InstructionKind::Csrrsi ||
            decoded.kind == InstructionKind::Csrrci;
        const bool set_or_clear =
            decoded.kind == InstructionKind::Csrrs ||
            decoded.kind == InstructionKind::Csrrc ||
            decoded.kind == InstructionKind::Csrrsi ||
            decoded.kind == InstructionKind::Csrrci;
        const Xlen source = immediate ? decoded.immediate : rs1;
        const bool read_required =
            decoded.kind != InstructionKind::Csrrw &&
            decoded.kind != InstructionKind::Csrrwi
                ? true
                : decoded.rd != 0U;
        const bool write_required =
            !set_or_clear ||
            (immediate ? decoded.immediate != 0U
                       : decoded.rs1 != 0U);
        if (write_required &&
            csr_file.validate_write(
                decoded.csr,
                executing_privilege) != CsrAccessStatus::Ready) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        Xlen old_value = 0;
        if (read_required) {
            const CsrReadResult read =
                csr_file.read(decoded.csr, executing_privilege);
            if (!read.ready()) {
                return trap(
                    ExceptionCause::IllegalInstruction,
                    instruction,
                    instruction);
            }
            old_value = read.value;
        }
        register_value = old_value;
        if (write_required) {
            Xlen base_value =
                set_or_clear
                    ? csr_file.read_for_write(
                          decoded.csr,
                          old_value)
                    : old_value;
            Xlen new_value = source;
            if (decoded.kind == InstructionKind::Csrrs ||
                decoded.kind == InstructionKind::Csrrsi) {
                new_value = base_value | source;
            } else if (
                decoded.kind == InstructionKind::Csrrc ||
                decoded.kind == InstructionKind::Csrrci) {
                new_value = base_value & ~source;
            }
            csr_write = std::pair{decoded.csr, new_value};
        }
        break;
    }
    case InstructionKind::Ecall:
        return trap(
            environment_call_cause(executing_privilege),
            instruction,
            0);
    case InstructionKind::Ebreak:
        return trap(
            ExceptionCause::Breakpoint,
            instruction,
            pc);
    case InstructionKind::Mret: {
        CpuSnapshot next = state_;
        if (!execute_mret(next)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        next_pc = next.pc;
        privileged_state = next;
        break;
    }
    case InstructionKind::Sret: {
        CpuSnapshot next = state_;
        if (!execute_sret(next)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        next_pc = next.pc;
        privileged_state = next;
        break;
    }
    case InstructionKind::Wfi:
        if (!wfi_allowed(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        wait_for_interrupt = true;
        break;
    case InstructionKind::SfenceVma:
        if (!sfence_vma_allowed(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        break;
    case InstructionKind::Illegal:
        return trap(
            ExceptionCause::IllegalInstruction,
            instruction,
            instruction);
    }

    if ((next_pc & 0x1U) != 0U) {
        return trap(
            ExceptionCause::InstructionAddressMisaligned,
            instruction,
            next_pc,
            rv::BusFault::Misaligned);
    }

    if (privileged_state.has_value()) {
        state_ = *privileged_state;
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
    state_.waiting_for_interrupt = wait_for_interrupt;
    if (csr_write.has_value()) {
        csr_file.write_validated(
            csr_write->first,
            csr_write->second);
    }
    return {
        .status = StepStatus::Retired,
        .privilege = executing_privilege,
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

const IrqLines& Core::sampled_irq_lines() const noexcept
{
    return sampled_irq_lines_;
}

} // namespace rv64
