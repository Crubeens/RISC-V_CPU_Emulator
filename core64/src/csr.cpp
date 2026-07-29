#include "rv64/core/csr.hpp"

#include "rv64/core/interrupt.hpp"

namespace rv64 {

namespace {

[[nodiscard]] constexpr bool privilege_allows(
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    const auto required =
        static_cast<std::uint8_t>((address >> 8U) & 0x3U);
    return static_cast<std::uint8_t>(privilege) >= required;
}

[[nodiscard]] constexpr bool is_counter(
    CsrAddress address) noexcept
{
    return address == csr_address::cycle ||
           address == csr_address::time ||
           address == csr_address::instret;
}

[[nodiscard]] constexpr Xlen counter_enable_bit(
    CsrAddress address) noexcept
{
    if (address == csr_address::cycle) {
        return Xlen{1} << 0U;
    }
    if (address == csr_address::time) {
        return Xlen{1} << 1U;
    }
    if (address == csr_address::instret) {
        return Xlen{1} << 2U;
    }
    return 0;
}

[[nodiscard]] bool counter_allowed(
    const CpuSnapshot& state,
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    if (privilege == PrivilegeMode::Machine) {
        return true;
    }
    const Xlen enable = counter_enable_bit(address);
    if ((state.machine_csrs.mcounteren & enable) == 0U) {
        return false;
    }
    return privilege != PrivilegeMode::User ||
           (state.supervisor_csrs.scounteren & enable) != 0U;
}

[[nodiscard]] constexpr bool machine_identity(
    CsrAddress address) noexcept
{
    return address == csr_address::mvendorid ||
           address == csr_address::marchid ||
           address == csr_address::mimpid ||
           address == csr_address::mhartid;
}

[[nodiscard]] constexpr bool machine_writable(
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
    case csr_address::mscratch:
    case csr_address::mepc:
    case csr_address::mcause:
    case csr_address::mtval:
    case csr_address::mip:
    case csr_address::mcycle:
    case csr_address::minstret:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool supervisor_writable(
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

[[nodiscard]] bool satp_trapped(
    const CpuSnapshot& state,
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    return address == csr_address::satp &&
           privilege == PrivilegeMode::Supervisor &&
           (state.machine_csrs.mstatus & mstatus_bits::tvm) != 0U;
}

} // namespace

Xlen sanitize_mstatus(Xlen value) noexcept
{
    Xlen result =
        (value & mstatus_bits::writable) | mstatus_bits::fixed;
    const Xlen previous =
        (result & mstatus_bits::mpp) >> mstatus_bits::mpp_shift;
    if (previous == 2U) {
        result &= ~mstatus_bits::mpp;
    }
    return result;
}

Xlen sanitize_tvec(Xlen value) noexcept
{
    const Xlen mode = value & 0x3U;
    return (value & ~Xlen{3}) | (mode <= 1U ? mode : 0U);
}

CsrFile::CsrFile(CpuSnapshot& state, rv::CpuBus& bus) noexcept
    : state_(&state), bus_(&bus)
{
}

CsrReadResult CsrFile::read(
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    if (!privilege_allows(address, privilege) ||
        satp_trapped(*state_, address, privilege) ||
        (is_counter(address) &&
         !counter_allowed(*state_, address, privilege))) {
        return {.status = CsrAccessStatus::PrivilegeViolation};
    }

    switch (address) {
    case csr_address::sstatus:
        return {
            .status = CsrAccessStatus::Ready,
            .value =
                sanitize_mstatus(state_->machine_csrs.mstatus) &
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
        return {CsrAccessStatus::Ready,
                state_->supervisor_csrs.stvec};
    case csr_address::scounteren:
        return {CsrAccessStatus::Ready,
                state_->supervisor_csrs.scounteren};
    case csr_address::sscratch:
        return {CsrAccessStatus::Ready,
                state_->supervisor_csrs.sscratch};
    case csr_address::sepc:
        return {CsrAccessStatus::Ready,
                state_->supervisor_csrs.sepc};
    case csr_address::scause:
        return {CsrAccessStatus::Ready,
                state_->supervisor_csrs.scause};
    case csr_address::stval:
        return {CsrAccessStatus::Ready,
                state_->supervisor_csrs.stval};
    case csr_address::sip:
        return {
            .status = CsrAccessStatus::Ready,
            .value =
                pending_interrupts(*state_) &
                state_->machine_csrs.mideleg &
                interrupt_bits::supervisor,
        };
    case csr_address::satp:
        return {CsrAccessStatus::Ready,
                state_->supervisor_csrs.satp};
    case csr_address::mstatus:
        return {CsrAccessStatus::Ready,
                sanitize_mstatus(state_->machine_csrs.mstatus)};
    case csr_address::misa:
        return {CsrAccessStatus::Ready, machine_isa_value};
    case csr_address::medeleg:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.medeleg};
    case csr_address::mideleg:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.mideleg};
    case csr_address::mie:
        return {
            .status = CsrAccessStatus::Ready,
            .value =
                state_->machine_csrs.mie &
                interrupt_bits::supported,
        };
    case csr_address::mtvec:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.mtvec};
    case csr_address::mcounteren:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.mcounteren};
    case csr_address::mscratch:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.mscratch};
    case csr_address::mepc:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.mepc};
    case csr_address::mcause:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.mcause};
    case csr_address::mtval:
        return {CsrAccessStatus::Ready,
                state_->machine_csrs.mtval};
    case csr_address::mip:
        return {CsrAccessStatus::Ready,
                pending_interrupts(*state_)};
    case csr_address::mcycle:
    case csr_address::cycle:
        return {CsrAccessStatus::Ready, state_->cycle};
    case csr_address::time:
        return {CsrAccessStatus::Ready, bus_->read_time()};
    case csr_address::minstret:
    case csr_address::instret:
        return {CsrAccessStatus::Ready,
                state_->instructions_retired};
    case csr_address::mvendorid:
    case csr_address::marchid:
    case csr_address::mimpid:
        return {CsrAccessStatus::Ready, 0};
    case csr_address::mhartid:
        return {CsrAccessStatus::Ready, state_->hart_id};
    default:
        return {.status = CsrAccessStatus::NotFound};
    }
}

CsrAccessStatus CsrFile::validate_write(
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    if (!privilege_allows(address, privilege) ||
        satp_trapped(*state_, address, privilege)) {
        return CsrAccessStatus::PrivilegeViolation;
    }
    if (is_counter(address) || machine_identity(address)) {
        return CsrAccessStatus::ReadOnly;
    }
    if (machine_writable(address) || supervisor_writable(address)) {
        return CsrAccessStatus::Ready;
    }
    return CsrAccessStatus::NotFound;
}

void CsrFile::write_validated(
    CsrAddress address,
    Xlen value) noexcept
{
    switch (address) {
    case csr_address::sstatus:
        state_->machine_csrs.mstatus = sanitize_mstatus(
            (state_->machine_csrs.mstatus &
             ~mstatus_bits::supervisor_view) |
            (value & mstatus_bits::supervisor_view));
        break;
    case csr_address::sie: {
        const Xlen mask =
            state_->machine_csrs.mideleg &
            interrupt_bits::supervisor;
        state_->machine_csrs.mie =
            (state_->machine_csrs.mie & ~mask) |
            (value & mask);
        break;
    }
    case csr_address::stvec:
        state_->supervisor_csrs.stvec = sanitize_tvec(value);
        break;
    case csr_address::scounteren:
        state_->supervisor_csrs.scounteren =
            value & supported_counter_enable;
        break;
    case csr_address::sscratch:
        state_->supervisor_csrs.sscratch = value;
        break;
    case csr_address::sepc:
        state_->supervisor_csrs.sepc = value & ~Xlen{1};
        break;
    case csr_address::scause:
        state_->supervisor_csrs.scause = value;
        break;
    case csr_address::stval:
        state_->supervisor_csrs.stval = value;
        break;
    case csr_address::sip: {
        const Xlen mask =
            state_->machine_csrs.mideleg &
            interrupt_bits::supervisor_writable_pending;
        state_->machine_csrs.mip_software =
            (state_->machine_csrs.mip_software & ~mask) |
            (value & mask);
        break;
    }
    case csr_address::satp: {
        const Xlen mode = value >> 60U;
        if (mode == 0U || mode == 8U) {
            state_->supervisor_csrs.satp = value;
        }
        break;
    }
    case csr_address::mstatus:
        state_->machine_csrs.mstatus = sanitize_mstatus(value);
        break;
    case csr_address::misa:
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
    case csr_address::mtvec:
        state_->machine_csrs.mtvec = sanitize_tvec(value);
        break;
    case csr_address::mcounteren:
        state_->machine_csrs.mcounteren =
            value & supported_counter_enable;
        break;
    case csr_address::mscratch:
        state_->machine_csrs.mscratch = value;
        break;
    case csr_address::mepc:
        state_->machine_csrs.mepc = value & ~Xlen{1};
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
        state_->cycle = value;
        break;
    case csr_address::minstret:
        state_->instructions_retired = value;
        break;
    default:
        break;
    }
}

Xlen CsrFile::read_for_write(
    CsrAddress address,
    Xlen read_value) noexcept
{
    if (address == csr_address::mip) {
        return
            (read_value & ~interrupt_bits::machine_writable_pending) |
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

} // namespace rv64
