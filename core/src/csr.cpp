#include "rv32/core/csr.hpp"

#include "rv32/core/interrupt.hpp"
#include "rv32/core/mmu.hpp"

namespace rv32 {

namespace {

[[nodiscard]] constexpr bool is_zicntr_address(
    CsrAddress address) noexcept
{
    switch (address) {
    case csr_address::cycle:
    case csr_address::time:
    case csr_address::instret:
    case csr_address::cycleh:
    case csr_address::timeh:
    case csr_address::instreth:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr std::uint32_t counter_enable_bit(
    CsrAddress address) noexcept
{
    switch (address) {
    case csr_address::cycle:
    case csr_address::cycleh:
        return 1U << 0U;
    case csr_address::time:
    case csr_address::timeh:
        return 1U << 1U;
    case csr_address::instret:
    case csr_address::instreth:
        return 1U << 2U;
    default:
        return 0;
    }
}

[[nodiscard]] bool counter_read_allowed(
    const CpuSnapshot& state,
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    if (privilege == PrivilegeMode::Machine) {
        return true;
    }

    const std::uint32_t enable = counter_enable_bit(address);
    if ((state.machine_csrs.mcounteren & enable) == 0U) {
        return false;
    }
    return privilege != PrivilegeMode::User ||
           (state.supervisor_csrs.scounteren & enable) != 0U;
}

[[nodiscard]] constexpr bool is_machine_identity_address(
    CsrAddress address) noexcept
{
    switch (address) {
    case csr_address::mvendorid:
    case csr_address::marchid:
    case csr_address::mimpid:
    case csr_address::mhartid:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool is_writable_machine_address(
    CsrAddress address) noexcept
{
    switch (address) {
    case csr_address::mstatus:
    case csr_address::misa:
    case csr_address::medeleg:
    case csr_address::mideleg:
    case csr_address::mie:
    case csr_address::mtvec:
    case csr_address::mcounteren:
    case csr_address::mstatush:
    case csr_address::medelegh:
    case csr_address::mscratch:
    case csr_address::mepc:
    case csr_address::mcause:
    case csr_address::mtval:
    case csr_address::mip:
    case csr_address::mcycle:
    case csr_address::minstret:
    case csr_address::mcycleh:
    case csr_address::minstreth:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool is_writable_supervisor_address(
    CsrAddress address) noexcept
{
    switch (address) {
    case csr_address::sstatus:
    case csr_address::sie:
    case csr_address::stvec:
    case csr_address::scounteren:
    case csr_address::sscratch:
    case csr_address::sepc:
    case csr_address::scause:
    case csr_address::stval:
    case csr_address::sip:
    case csr_address::satp:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr std::uint32_t low_word(
    std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t high_word(
    std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>(value >> 32U);
}

[[nodiscard]] bool satp_access_trapped(
    const CpuSnapshot& state,
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    return
        address == csr_address::satp &&
        privilege == PrivilegeMode::Supervisor &&
        (state.machine_csrs.mstatus & mstatus_bits::tvm) != 0U;
}

} // namespace

std::uint32_t sanitize_mstatus(std::uint32_t value) noexcept
{
    std::uint32_t sanitized =
        value & mstatus_bits::implemented;
    const std::uint32_t previous_privilege =
        (sanitized & mstatus_bits::mpp) >>
        mstatus_bits::mpp_shift;
    if (previous_privilege == 0x2U) {
        // MPP is WARL and this core implements only U=0, S=1, and M=3.
        // Coerce the reserved encoding to the least-privileged mode.
        sanitized &= ~mstatus_bits::mpp;
    }
    return sanitized;
}

std::uint32_t sanitize_mtvec(std::uint32_t value) noexcept
{
    const std::uint32_t base = value & ~0x3U;
    const std::uint32_t mode = value & 0x3U;
    return base | (mode <= 1U ? mode : 0U);
}

CsrFile::CsrFile(CpuSnapshot& state, CpuBus& bus) noexcept
    : state_(&state), bus_(&bus)
{
}

CsrReadResult CsrFile::read(
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    if (!csr_privilege_allows(address, privilege)) {
        return {
            .status = CsrAccessStatus::PrivilegeViolation,
            .value = 0,
        };
    }
    if (satp_access_trapped(*state_, address, privilege)) {
        return {
            .status = CsrAccessStatus::PrivilegeViolation,
            .value = 0,
        };
    }
    if (is_zicntr_address(address) &&
        !counter_read_allowed(*state_, address, privilege)) {
        return {
            .status = CsrAccessStatus::PrivilegeViolation,
            .value = 0,
        };
    }

    switch (address) {
    case csr_address::sstatus:
        return {
            .status = CsrAccessStatus::Ready,
            .value =
                state_->machine_csrs.mstatus &
                mstatus_bits::supervisor_view,
        };
    case csr_address::sie:
        return {
            .status = CsrAccessStatus::Ready,
            .value =
                state_->machine_csrs.mie &
                state_->machine_csrs.mideleg &
                interrupt_bits::supervisor,
        };
    case csr_address::stvec:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->supervisor_csrs.stvec,
        };
    case csr_address::scounteren:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->supervisor_csrs.scounteren,
        };
    case csr_address::sscratch:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->supervisor_csrs.sscratch,
        };
    case csr_address::sepc:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->supervisor_csrs.sepc,
        };
    case csr_address::scause:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->supervisor_csrs.scause,
        };
    case csr_address::stval:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->supervisor_csrs.stval,
        };
    case csr_address::sip:
        return {
            .status = CsrAccessStatus::Ready,
            .value =
                pending_interrupts(*state_) &
                state_->machine_csrs.mideleg &
                interrupt_bits::supervisor,
        };
    case csr_address::satp:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->supervisor_csrs.satp,
        };
    case csr_address::mstatus:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mstatus,
        };
    case csr_address::misa:
        return {
            .status = CsrAccessStatus::Ready,
            .value = machine_isa_value,
        };
    case csr_address::medeleg:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.medeleg,
        };
    case csr_address::mideleg:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mideleg,
        };
    case csr_address::mie:
        return {
            .status = CsrAccessStatus::Ready,
            .value =
                state_->machine_csrs.mie &
                interrupt_bits::supported,
        };
    case csr_address::mcounteren:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mcounteren,
        };
    case csr_address::mstatush:
    case csr_address::medelegh:
        return {
            .status = CsrAccessStatus::Ready,
            .value = 0,
        };
    case csr_address::mtvec:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mtvec,
        };
    case csr_address::mscratch:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mscratch,
        };
    case csr_address::mepc:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mepc,
        };
    case csr_address::mcause:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mcause,
        };
    case csr_address::mtval:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->machine_csrs.mtval,
        };
    case csr_address::mip:
        return {
            .status = CsrAccessStatus::Ready,
            .value = pending_interrupts(*state_),
        };
    case csr_address::mcycle:
    case csr_address::cycle:
        return {
            .status = CsrAccessStatus::Ready,
            .value = low_word(state_->cycle),
        };
    case csr_address::time:
        return {
            .status = CsrAccessStatus::Ready,
            .value = low_word(bus_->read_time()),
        };
    case csr_address::minstret:
    case csr_address::instret:
        return {
            .status = CsrAccessStatus::Ready,
            .value = low_word(state_->instructions_retired),
        };
    case csr_address::mcycleh:
    case csr_address::cycleh:
        return {
            .status = CsrAccessStatus::Ready,
            .value = high_word(state_->cycle),
        };
    case csr_address::timeh:
        return {
            .status = CsrAccessStatus::Ready,
            .value = high_word(bus_->read_time()),
        };
    case csr_address::minstreth:
    case csr_address::instreth:
        return {
            .status = CsrAccessStatus::Ready,
            .value = high_word(state_->instructions_retired),
        };
    case csr_address::mvendorid:
    case csr_address::marchid:
    case csr_address::mimpid:
        return {
            .status = CsrAccessStatus::Ready,
            .value = 0,
        };
    case csr_address::mhartid:
        return {
            .status = CsrAccessStatus::Ready,
            .value = state_->hart_id,
        };
    default:
        return {
            .status = CsrAccessStatus::NotFound,
            .value = 0,
        };
    }
}

CsrAccessStatus CsrFile::validate_write(
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    if (!csr_privilege_allows(address, privilege)) {
        return CsrAccessStatus::PrivilegeViolation;
    }
    if (satp_access_trapped(*state_, address, privilege)) {
        return CsrAccessStatus::PrivilegeViolation;
    }
    if (is_zicntr_address(address) ||
        is_machine_identity_address(address)) {
        return CsrAccessStatus::ReadOnly;
    }
    if (is_writable_machine_address(address) ||
        is_writable_supervisor_address(address)) {
        return CsrAccessStatus::Ready;
    }
    return CsrAccessStatus::NotFound;
}

void CsrFile::write_validated(
    CsrAddress address,
    std::uint32_t value) noexcept
{
    switch (address) {
    case csr_address::sstatus:
        state_->machine_csrs.mstatus = sanitize_mstatus(
            (state_->machine_csrs.mstatus &
             ~mstatus_bits::supervisor_view) |
            (value & mstatus_bits::supervisor_view));
        break;
    case csr_address::sie: {
        const std::uint32_t mask =
            state_->machine_csrs.mideleg &
            interrupt_bits::supervisor;
        state_->machine_csrs.mie =
            (state_->machine_csrs.mie & ~mask) |
            (value & mask);
        break;
    }
    case csr_address::stvec:
        state_->supervisor_csrs.stvec = sanitize_mtvec(value);
        break;
    case csr_address::scounteren:
        state_->supervisor_csrs.scounteren =
            value & supported_counter_enable;
        break;
    case csr_address::sscratch:
        state_->supervisor_csrs.sscratch = value;
        break;
    case csr_address::sepc:
        state_->supervisor_csrs.sepc = value & ~0x1U;
        break;
    case csr_address::scause:
        state_->supervisor_csrs.scause = value;
        break;
    case csr_address::stval:
        state_->supervisor_csrs.stval = value;
        break;
    case csr_address::sip: {
        const std::uint32_t mask =
            state_->machine_csrs.mideleg &
            interrupt_bits::supervisor_writable_pending;
        state_->machine_csrs.mip_software =
            (state_->machine_csrs.mip_software & ~mask) |
            (value & mask);
        break;
    }
    case csr_address::satp:
        state_->supervisor_csrs.satp = sanitize_satp(value);
        break;
    case csr_address::mstatus:
        state_->machine_csrs.mstatus = sanitize_mstatus(value);
        break;
    case csr_address::misa:
        // The supported ISA is fixed. misa remains WARL and accepts writes,
        // but every write is coerced back to the implemented configuration.
        break;
    case csr_address::medeleg:
        state_->machine_csrs.medeleg =
            value & supported_exception_delegation;
        break;
    case csr_address::mideleg:
        state_->machine_csrs.mideleg =
            value & supported_interrupt_delegation;
        break;
    case csr_address::mie:
        state_->machine_csrs.mie =
            value & interrupt_bits::supported;
        break;
    case csr_address::mcounteren:
        state_->machine_csrs.mcounteren =
            value & supported_counter_enable;
        break;
    case csr_address::mstatush:
    case csr_address::medelegh:
        // No implemented RV32-only high status/delegation bits are writable.
        break;
    case csr_address::mtvec:
        state_->machine_csrs.mtvec = sanitize_mtvec(value);
        break;
    case csr_address::mscratch:
        state_->machine_csrs.mscratch = value;
        break;
    case csr_address::mepc:
        state_->machine_csrs.mepc = value & ~0x1U;
        break;
    case csr_address::mcause:
        state_->machine_csrs.mcause = value;
        break;
    case csr_address::mtval:
        state_->machine_csrs.mtval = value;
        break;
    case csr_address::mip:
        state_->machine_csrs.mip_software =
            value & interrupt_bits::machine_writable_pending;
        break;
    case csr_address::mcycle:
        state_->cycle =
            (state_->cycle & 0xFFFFFFFF00000000ULL) |
            value;
        break;
    case csr_address::minstret:
        state_->instructions_retired =
            (state_->instructions_retired &
             0xFFFFFFFF00000000ULL) |
            value;
        break;
    case csr_address::mcycleh:
        state_->cycle =
            (state_->cycle & 0x00000000FFFFFFFFULL) |
            (static_cast<std::uint64_t>(value) << 32U);
        break;
    case csr_address::minstreth:
        state_->instructions_retired =
            (state_->instructions_retired &
             0x00000000FFFFFFFFULL) |
            (static_cast<std::uint64_t>(value) << 32U);
        break;
    default:
        break;
    }
}

std::uint32_t CsrFile::read_for_write(
    CsrAddress address,
    std::uint32_t read_value) noexcept
{
    if (address == csr_address::mip) {
        return
            (read_value &
             ~interrupt_bits::machine_writable_pending) |
            (state_->machine_csrs.mip_software &
             interrupt_bits::machine_writable_pending);
    }
    if (address == csr_address::sip) {
        return
            (read_value &
             ~interrupt_bits::supervisor_writable_pending) |
            (state_->machine_csrs.mip_software &
             interrupt_bits::supervisor_writable_pending);
    }
    return read_value;
}

} // namespace rv32
