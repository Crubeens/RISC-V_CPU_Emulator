#include "rv64/core/core.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "rv64/core/csr.hpp"
#include "rv64/core/decode.hpp"
#include "rv64/core/floating.hpp"
#include "rv64/core/interrupt.hpp"
#include "rv64/core/mmu.hpp"
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
    tlb_.clear();
    clear_decode_cache(false);
    performance_counters_ = {};
}

StepResult Core::step(const IrqLines& irq_lines)
{
    ++performance_counters_.step_calls;
    ++state_.cycle;
    state_.registers[0] = 0;
    sampled_irq_lines_ = irq_lines;
    sample_interrupt_lines(state_, irq_lines);

    if (state_.waiting_for_interrupt) {
        if (!interrupt_wake_requested(state_)) {
            ++performance_counters_.waiting_returns;
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
        ++performance_counters_.interrupt_traps;
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
        ++performance_counters_.synchronous_traps;
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
    const auto translate =
        [&](Xlen virtual_address, MemoryAccessType access) {
        if (execution_mode_ == ExecutionMode::Fast) {
            return tlb_.translate(
                *bus_,
                state_,
                virtual_address,
                access,
                &performance_counters_.mmu);
        }
        return translate_address(
            *bus_,
            state_,
            virtual_address,
            access,
            &performance_counters_.mmu);
    };

    if ((pc & 0x1U) != 0U) {
        return trap(
            ExceptionCause::InstructionAddressMisaligned,
            0,
            pc,
            rv::BusFault::Misaligned);
    }

    ++performance_counters_.fetch.instruction_fetches;
    const TranslationResult first_translation = translate(
        pc,
        MemoryAccessType::InstructionFetch);
    if (!first_translation.ready()) {
        return trap(
            first_translation.status == TranslationStatus::PageFault
                ? ExceptionCause::InstructionPageFault
                : ExceptionCause::InstructionAccessFault,
            0,
            pc,
            first_translation.bus_fault);
    }
    ++performance_counters_.fetch.halfword_reads;
    const rv::ReadResult first = bus_->read(
        first_translation.physical_address,
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
        ++performance_counters_.fetch.compressed_instructions;
        decoded = decode(instruction, 2U);
    } else {
        ++performance_counters_.fetch.standard_instructions;
        const TranslationResult second_translation = translate(
            pc + 2U,
            MemoryAccessType::InstructionFetch);
        if (!second_translation.ready()) {
            return trap(
                second_translation.status ==
                        TranslationStatus::PageFault
                    ? ExceptionCause::InstructionPageFault
                    : ExceptionCause::InstructionAccessFault,
                instruction,
                pc + 2U,
                second_translation.bus_fault);
        }
        ++performance_counters_.fetch.halfword_reads;
        const rv::ReadResult second = bus_->read(
            second_translation.physical_address,
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
        decoded = decode(instruction, 4U);
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
    std::optional<std::uint64_t> floating_register_value;
    std::uint8_t floating_exception_flags = 0;
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
        const MemoryAccessType access =
            load_reserved ? MemoryAccessType::Load
                          : MemoryAccessType::Store;
        const TranslationResult translation = translate(
            address,
            access);
        if (!translation.ready()) {
            const bool page_fault =
                translation.status ==
                TranslationStatus::PageFault;
            return trap(
                load_reserved
                    ? (page_fault
                           ? ExceptionCause::LoadPageFault
                           : ExceptionCause::LoadAccessFault)
                    : (page_fault
                           ? ExceptionCause::StorePageFault
                           : ExceptionCause::StoreAccessFault),
                instruction,
                address,
                translation.bus_fault);
        }
        const rv::PhysAddr physical_address =
            translation.physical_address;

        if (load_reserved) {
            const rv::ReadResult result =
                doubleword
                    ? bus_->load_reserved_doubleword(
                          static_cast<std::uint32_t>(state_.hart_id),
                          physical_address)
                    : bus_->load_reserved_word(
                          static_cast<std::uint32_t>(state_.hart_id),
                          physical_address);
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
                          physical_address,
                          rs2)
                    : bus_->store_conditional_word(
                          static_cast<std::uint32_t>(state_.hart_id),
                          physical_address,
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
                    physical_address,
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
                physical_address,
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
        const TranslationResult translation = translate(
            address,
            MemoryAccessType::Load);
        if (!translation.ready()) {
            return trap(
                translation.status == TranslationStatus::PageFault
                    ? ExceptionCause::LoadPageFault
                    : ExceptionCause::LoadAccessFault,
                instruction,
                address,
                translation.bus_fault);
        }
        const rv::ReadResult loaded =
            bus_->read(
                translation.physical_address,
                width,
                rv::AccessKind::Load);
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
    case InstructionKind::Flw:
    case InstructionKind::Fld: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        const rv::AccessWidth width =
            decoded.kind == InstructionKind::Flw
                ? rv::AccessWidth::Word
                : rv::AccessWidth::DoubleWord;
        const std::uint64_t address = rs1 + decoded.immediate;
        if (!aligned(address, width)) {
            return trap(
                ExceptionCause::LoadAddressMisaligned,
                instruction,
                address,
                rv::BusFault::Misaligned);
        }
        const TranslationResult translation = translate(
            address,
            MemoryAccessType::Load);
        if (!translation.ready()) {
            return trap(
                translation.status == TranslationStatus::PageFault
                    ? ExceptionCause::LoadPageFault
                    : ExceptionCause::LoadAccessFault,
                instruction,
                address,
                translation.bus_fault);
        }
        const rv::ReadResult loaded = bus_->read(
            translation.physical_address,
            width,
            rv::AccessKind::Load);
        if (!loaded.ok()) {
            return trap(
                ExceptionCause::LoadAccessFault,
                instruction,
                address,
                loaded.fault);
        }
        floating_register_value =
            decoded.kind == InstructionKind::Flw
                ? (0xFFFFFFFF00000000ULL |
                   static_cast<std::uint32_t>(loaded.value))
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
        const TranslationResult translation = translate(
            address,
            MemoryAccessType::Store);
        if (!translation.ready()) {
            return trap(
                translation.status == TranslationStatus::PageFault
                    ? ExceptionCause::StorePageFault
                    : ExceptionCause::StoreAccessFault,
                instruction,
                address,
                translation.bus_fault);
        }
        const rv::BusFault fault = bus_->write(
            translation.physical_address,
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
    case InstructionKind::Fsw:
    case InstructionKind::Fsd: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        const rv::AccessWidth width =
            decoded.kind == InstructionKind::Fsw
                ? rv::AccessWidth::Word
                : rv::AccessWidth::DoubleWord;
        const std::uint64_t address = rs1 + decoded.immediate;
        if (!aligned(address, width)) {
            return trap(
                ExceptionCause::StoreAddressMisaligned,
                instruction,
                address,
                rv::BusFault::Misaligned);
        }
        const TranslationResult translation = translate(
            address,
            MemoryAccessType::Store);
        if (!translation.ready()) {
            return trap(
                translation.status == TranslationStatus::PageFault
                    ? ExceptionCause::StorePageFault
                    : ExceptionCause::StoreAccessFault,
                instruction,
                address,
                translation.bus_fault);
        }
        const rv::BusFault fault = bus_->write(
            translation.physical_address,
            width,
            state_.floating_point.registers[decoded.rs2],
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
    case InstructionKind::FmvXW:
    case InstructionKind::FmvWX:
    case InstructionKind::FmvXD:
    case InstructionKind::FmvDX:
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        if (decoded.kind == InstructionKind::FmvXW) {
            register_value = sign_extend_word(
                state_.floating_point.registers[decoded.rs1]);
        } else if (decoded.kind == InstructionKind::FmvWX) {
            floating_register_value =
                0xFFFFFFFF00000000ULL |
                static_cast<std::uint32_t>(rs1);
        } else if (decoded.kind == InstructionKind::FmvXD) {
            register_value =
                state_.floating_point.registers[decoded.rs1];
        } else {
            floating_register_value = rs1;
        }
        break;
    case InstructionKind::FaddS:
    case InstructionKind::FsubS:
    case InstructionKind::FmulS:
    case InstructionKind::FdivS:
    case InstructionKind::FsqrtS:
    case InstructionKind::FmaddS:
    case InstructionKind::FmsubS:
    case InstructionKind::FnmsubS:
    case InstructionKind::FnmaddS:
    case InstructionKind::FaddD:
    case InstructionKind::FsubD:
    case InstructionKind::FmulD:
    case InstructionKind::FdivD:
    case InstructionKind::FsqrtD:
    case InstructionKind::FmaddD:
    case InstructionKind::FmsubD:
    case InstructionKind::FnmsubD:
    case InstructionKind::FnmaddD: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        std::uint8_t rounding_mode = 0;
        if (!resolve_rounding_mode(
                decoded.rounding_mode,
                state_.floating_point.fcsr,
                rounding_mode)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }

        const bool double_precision =
            decoded.kind == InstructionKind::FaddD ||
            decoded.kind == InstructionKind::FsubD ||
            decoded.kind == InstructionKind::FmulD ||
            decoded.kind == InstructionKind::FdivD ||
            decoded.kind == InstructionKind::FsqrtD ||
            decoded.kind == InstructionKind::FmaddD ||
            decoded.kind == InstructionKind::FmsubD ||
            decoded.kind == InstructionKind::FnmsubD ||
            decoded.kind == InstructionKind::FnmaddD;
        FloatingArithmeticOperation operation =
            FloatingArithmeticOperation::Add;
        switch (decoded.kind) {
        case InstructionKind::FsubS:
        case InstructionKind::FsubD:
            operation = FloatingArithmeticOperation::Subtract;
            break;
        case InstructionKind::FmulS:
        case InstructionKind::FmulD:
            operation = FloatingArithmeticOperation::Multiply;
            break;
        case InstructionKind::FdivS:
        case InstructionKind::FdivD:
            operation = FloatingArithmeticOperation::Divide;
            break;
        case InstructionKind::FsqrtS:
        case InstructionKind::FsqrtD:
            operation = FloatingArithmeticOperation::SquareRoot;
            break;
        case InstructionKind::FmaddS:
        case InstructionKind::FmaddD:
            operation = FloatingArithmeticOperation::MultiplyAdd;
            break;
        case InstructionKind::FmsubS:
        case InstructionKind::FmsubD:
            operation = FloatingArithmeticOperation::MultiplySubtract;
            break;
        case InstructionKind::FnmsubS:
        case InstructionKind::FnmsubD:
            operation = FloatingArithmeticOperation::NegatedMultiplyAdd;
            break;
        case InstructionKind::FnmaddS:
        case InstructionKind::FnmaddD:
            operation =
                FloatingArithmeticOperation::NegatedMultiplySubtract;
            break;
        default:
            break;
        }

        const FloatingResult result = floating_arithmetic(
            double_precision ? FloatingFormat::Double
                             : FloatingFormat::Single,
            operation,
            rounding_mode,
            state_.floating_point.registers[decoded.rs1],
            state_.floating_point.registers[decoded.rs2],
            state_.floating_point.registers[decoded.rs3]);
        floating_register_value = result.value;
        floating_exception_flags = result.exception_flags;
        break;
    }
    case InstructionKind::FsgnjS:
    case InstructionKind::FsgnjnS:
    case InstructionKind::FsgnjxS:
    case InstructionKind::FsgnjD:
    case InstructionKind::FsgnjnD:
    case InstructionKind::FsgnjxD: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        FloatingSignOperation operation = FloatingSignOperation::Copy;
        if (decoded.kind == InstructionKind::FsgnjnS ||
            decoded.kind == InstructionKind::FsgnjnD) {
            operation = FloatingSignOperation::Negate;
        } else if (decoded.kind == InstructionKind::FsgnjxS ||
                   decoded.kind == InstructionKind::FsgnjxD) {
            operation = FloatingSignOperation::ExclusiveOr;
        }
        const bool double_precision =
            decoded.kind == InstructionKind::FsgnjD ||
            decoded.kind == InstructionKind::FsgnjnD ||
            decoded.kind == InstructionKind::FsgnjxD;
        floating_register_value = floating_sign_injection(
            double_precision ? FloatingFormat::Double
                             : FloatingFormat::Single,
            operation,
            state_.floating_point.registers[decoded.rs1],
            state_.floating_point.registers[decoded.rs2]);
        break;
    }
    case InstructionKind::FminS:
    case InstructionKind::FmaxS:
    case InstructionKind::FminD:
    case InstructionKind::FmaxD: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        const bool double_precision =
            decoded.kind == InstructionKind::FminD ||
            decoded.kind == InstructionKind::FmaxD;
        const bool maximum =
            decoded.kind == InstructionKind::FmaxS ||
            decoded.kind == InstructionKind::FmaxD;
        const FloatingResult result = floating_min_max(
            double_precision ? FloatingFormat::Double
                             : FloatingFormat::Single,
            maximum ? FloatingMinMaxOperation::Maximum
                    : FloatingMinMaxOperation::Minimum,
            state_.floating_point.registers[decoded.rs1],
            state_.floating_point.registers[decoded.rs2]);
        floating_register_value = result.value;
        floating_exception_flags = result.exception_flags;
        break;
    }
    case InstructionKind::FeqS:
    case InstructionKind::FltS:
    case InstructionKind::FleS:
    case InstructionKind::FeqD:
    case InstructionKind::FltD:
    case InstructionKind::FleD: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        FloatingComparisonOperation operation =
            FloatingComparisonOperation::Equal;
        if (decoded.kind == InstructionKind::FltS ||
            decoded.kind == InstructionKind::FltD) {
            operation = FloatingComparisonOperation::LessThan;
        } else if (decoded.kind == InstructionKind::FleS ||
                   decoded.kind == InstructionKind::FleD) {
            operation = FloatingComparisonOperation::LessOrEqual;
        }
        const bool double_precision =
            decoded.kind == InstructionKind::FeqD ||
            decoded.kind == InstructionKind::FltD ||
            decoded.kind == InstructionKind::FleD;
        const FloatingIntegerResult result = floating_compare(
            double_precision ? FloatingFormat::Double
                             : FloatingFormat::Single,
            operation,
            state_.floating_point.registers[decoded.rs1],
            state_.floating_point.registers[decoded.rs2]);
        register_value = result.value;
        floating_exception_flags = result.exception_flags;
        break;
    }
    case InstructionKind::FclassS:
    case InstructionKind::FclassD:
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        register_value = floating_classify(
            decoded.kind == InstructionKind::FclassD
                ? FloatingFormat::Double
                : FloatingFormat::Single,
            state_.floating_point.registers[decoded.rs1]);
        break;
    case InstructionKind::FcvtSD:
    case InstructionKind::FcvtDS: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        std::uint8_t rounding_mode = 0;
        if (!resolve_rounding_mode(
                decoded.rounding_mode,
                state_.floating_point.fcsr,
                rounding_mode)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        const FloatingResult result = floating_convert_format(
            decoded.kind == InstructionKind::FcvtSD
                ? FloatingFormat::Single
                : FloatingFormat::Double,
            rounding_mode,
            state_.floating_point.registers[decoded.rs1]);
        floating_register_value = result.value;
        floating_exception_flags = result.exception_flags;
        break;
    }
    case InstructionKind::FcvtWS:
    case InstructionKind::FcvtWuS:
    case InstructionKind::FcvtLS:
    case InstructionKind::FcvtLuS:
    case InstructionKind::FcvtWD:
    case InstructionKind::FcvtWuD:
    case InstructionKind::FcvtLD:
    case InstructionKind::FcvtLuD: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        std::uint8_t rounding_mode = 0;
        if (!resolve_rounding_mode(
                decoded.rounding_mode,
                state_.floating_point.fcsr,
                rounding_mode)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        const bool double_precision =
            decoded.kind == InstructionKind::FcvtWD ||
            decoded.kind == InstructionKind::FcvtWuD ||
            decoded.kind == InstructionKind::FcvtLD ||
            decoded.kind == InstructionKind::FcvtLuD;
        const bool long_integer =
            decoded.kind == InstructionKind::FcvtLS ||
            decoded.kind == InstructionKind::FcvtLuS ||
            decoded.kind == InstructionKind::FcvtLD ||
            decoded.kind == InstructionKind::FcvtLuD;
        const bool unsigned_integer =
            decoded.kind == InstructionKind::FcvtWuS ||
            decoded.kind == InstructionKind::FcvtLuS ||
            decoded.kind == InstructionKind::FcvtWuD ||
            decoded.kind == InstructionKind::FcvtLuD;
        const FloatingIntegerResult result = floating_to_integer(
            double_precision ? FloatingFormat::Double
                             : FloatingFormat::Single,
            long_integer ? FloatingIntegerWidth::Long
                         : FloatingIntegerWidth::Word,
            unsigned_integer,
            rounding_mode,
            state_.floating_point.registers[decoded.rs1]);
        register_value = result.value;
        floating_exception_flags = result.exception_flags;
        break;
    }
    case InstructionKind::FcvtSW:
    case InstructionKind::FcvtSWu:
    case InstructionKind::FcvtSL:
    case InstructionKind::FcvtSLu:
    case InstructionKind::FcvtDW:
    case InstructionKind::FcvtDWu:
    case InstructionKind::FcvtDL:
    case InstructionKind::FcvtDLu: {
        if (!floating_point_enabled(state_)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        std::uint8_t rounding_mode = 0;
        if (!resolve_rounding_mode(
                decoded.rounding_mode,
                state_.floating_point.fcsr,
                rounding_mode)) {
            return trap(
                ExceptionCause::IllegalInstruction,
                instruction,
                instruction);
        }
        const bool double_precision =
            decoded.kind == InstructionKind::FcvtDW ||
            decoded.kind == InstructionKind::FcvtDWu ||
            decoded.kind == InstructionKind::FcvtDL ||
            decoded.kind == InstructionKind::FcvtDLu;
        const bool long_integer =
            decoded.kind == InstructionKind::FcvtSL ||
            decoded.kind == InstructionKind::FcvtSLu ||
            decoded.kind == InstructionKind::FcvtDL ||
            decoded.kind == InstructionKind::FcvtDLu;
        const bool unsigned_integer =
            decoded.kind == InstructionKind::FcvtSWu ||
            decoded.kind == InstructionKind::FcvtSLu ||
            decoded.kind == InstructionKind::FcvtDWu ||
            decoded.kind == InstructionKind::FcvtDLu;
        const FloatingResult result = integer_to_floating(
            double_precision ? FloatingFormat::Double
                             : FloatingFormat::Single,
            long_integer ? FloatingIntegerWidth::Long
                         : FloatingIntegerWidth::Word,
            unsigned_integer,
            rounding_mode,
            rs1);
        floating_register_value = result.value;
        floating_exception_flags = result.exception_flags;
        break;
    }
    case InstructionKind::Fence:
        break;
    case InstructionKind::FenceI:
        clear_decode_cache(true);
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
        tlb_.sfence_vma(
            decoded.rs1 == 0U
                ? std::nullopt
                : std::optional<Xlen>{rs1},
            decoded.rs2 == 0U
                ? std::nullopt
                : std::optional<std::uint16_t>{
                      static_cast<std::uint16_t>(rs2)});
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
    FloatingRegisterCommit floating_register_write;
    if (floating_register_value.has_value()) {
        state_.floating_point.registers[decoded.rd] =
            *floating_register_value;
        mark_floating_point_dirty(state_);
        floating_register_write = {
            .enabled = true,
            .index = decoded.rd,
            .value = *floating_register_value,
        };
    }
    if (floating_exception_flags != 0U) {
        state_.floating_point.fcsr = static_cast<std::uint8_t>(
            state_.floating_point.fcsr |
            floating_exception_flags);
        mark_floating_point_dirty(state_);
    }
    state_.registers[0] = 0;
    state_.pc = next_pc;
    ++state_.instructions_retired;
    ++performance_counters_.retired_instructions;
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
        .floating_register_write = floating_register_write,
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

const CorePerformanceCounters&
Core::performance_counters() const noexcept
{
    return performance_counters_;
}

std::size_t Core::tlb_entries() const noexcept
{
    return tlb_.valid_entries();
}

void Core::set_execution_mode(ExecutionMode mode) noexcept
{
    if (execution_mode_ == mode) {
        return;
    }
    execution_mode_ = mode;
    tlb_.clear();
    clear_decode_cache(true);
}

ExecutionMode Core::execution_mode() const noexcept
{
    return execution_mode_;
}

std::size_t Core::decoded_entries() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        decode_cache_.begin(),
        decode_cache_.end(),
        [](const DecodeCacheEntry& entry) {
            return entry.valid;
        }));
}

DecodedInstruction Core::decode(
    std::uint32_t instruction,
    std::uint8_t length) noexcept
{
    const auto decode_direct =
        [instruction, length]() {
        return length == 2U
                   ? decode_compressed_instruction(
                         static_cast<std::uint16_t>(instruction))
                   : decode_instruction(instruction);
    };
    if (execution_mode_ == ExecutionMode::Reference) {
        return decode_direct();
    }

    auto& counters = performance_counters_.decode;
    ++counters.lookups;
    const std::uint32_t mixed =
        instruction ^ (instruction >> 11U) ^
        (static_cast<std::uint32_t>(length) << 29U);
    DecodeCacheEntry& entry =
        decode_cache_[mixed % decode_cache_.size()];
    if (entry.valid &&
        entry.length == length &&
        entry.instruction == instruction) {
        ++counters.hits;
        return entry.decoded;
    }

    ++counters.misses;
    entry = {
        .valid = true,
        .length = length,
        .instruction = instruction,
        .decoded = decode_direct(),
    };
    return entry.decoded;
}

void Core::clear_decode_cache(bool count_invalidation) noexcept
{
    decode_cache_ = {};
    if (count_invalidation) {
        ++performance_counters_.decode.invalidations;
    }
}

} // namespace rv64
