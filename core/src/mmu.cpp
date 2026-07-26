#include "rv32/core/mmu.hpp"

#include "rv32/core/csr.hpp"

namespace rv32 {

namespace {

constexpr std::uint32_t page_shift = 12U;
constexpr std::uint32_t vpn_mask = 0x3FFU;
constexpr std::uint32_t page_offset_mask = 0xFFFU;
constexpr std::uint32_t pte_ppn0_mask = 0x3FFU;

[[nodiscard]] constexpr TranslationResult ready(
    PhysAddr physical_address) noexcept
{
    return {
        .status = TranslationStatus::Ready,
        .physical_address = physical_address,
        .bus_fault = BusFault::None,
    };
}

[[nodiscard]] constexpr TranslationResult page_fault() noexcept
{
    return {
        .status = TranslationStatus::PageFault,
        .physical_address = 0,
        .bus_fault = BusFault::None,
    };
}

[[nodiscard]] constexpr TranslationResult access_fault(
    BusFault fault) noexcept
{
    return {
        .status = TranslationStatus::AccessFault,
        .physical_address = 0,
        .bus_fault = fault,
    };
}

[[nodiscard]] constexpr bool privilege_allows(
    std::uint32_t pte,
    PrivilegeMode privilege,
    MemoryAccessType access,
    std::uint32_t mstatus) noexcept
{
    const bool user_page =
        (pte & sv32_pte_bits::user) != 0U;
    if (privilege == PrivilegeMode::User) {
        if (!user_page) {
            return false;
        }
    } else if (privilege == PrivilegeMode::Supervisor &&
               user_page) {
        if (access == MemoryAccessType::InstructionFetch) {
            return false;
        }
        if ((mstatus & mstatus_bits::sum) == 0U) {
            return false;
        }
    }

    switch (access) {
    case MemoryAccessType::InstructionFetch:
        return (pte & sv32_pte_bits::execute) != 0U;
    case MemoryAccessType::Load:
        return
            (pte & sv32_pte_bits::read) != 0U ||
            ((mstatus & mstatus_bits::mxr) != 0U &&
             (pte & sv32_pte_bits::execute) != 0U);
    case MemoryAccessType::Store:
        return (pte & sv32_pte_bits::write) != 0U;
    }
    return false;
}

} // namespace

std::uint32_t sanitize_satp(std::uint32_t value) noexcept
{
    if ((value & satp_bits::mode) == 0U) {
        return 0;
    }
    return satp_bits::mode | (value & satp_bits::ppn);
}

PrivilegeMode effective_privilege(
    const CpuSnapshot& state,
    MemoryAccessType access) noexcept
{
    const std::uint32_t mstatus =
        sanitize_mstatus(state.machine_csrs.mstatus);
    if (access == MemoryAccessType::InstructionFetch ||
        (mstatus & mstatus_bits::mprv) == 0U) {
        return state.privilege;
    }

    const std::uint32_t encoded =
        (mstatus & mstatus_bits::mpp) >>
        mstatus_bits::mpp_shift;
    switch (encoded) {
    case 0U:
        return PrivilegeMode::User;
    case 1U:
        return PrivilegeMode::Supervisor;
    case 3U:
        return PrivilegeMode::Machine;
    default:
        return PrivilegeMode::User;
    }
}

TranslationResult translate_address(
    CpuBus& bus,
    const CpuSnapshot& state,
    std::uint32_t virtual_address,
    MemoryAccessType access)
{
    const PrivilegeMode privilege =
        effective_privilege(state, access);
    const std::uint32_t satp =
        sanitize_satp(state.supervisor_csrs.satp);
    if (privilege == PrivilegeMode::Machine ||
        (satp & satp_bits::mode) == 0U) {
        return ready(static_cast<PhysAddr>(virtual_address));
    }

    const std::uint32_t vpn[2]{
        (virtual_address >> 12U) & vpn_mask,
        (virtual_address >> 22U) & vpn_mask,
    };
    PhysAddr table_address =
        static_cast<PhysAddr>(satp & satp_bits::ppn)
        << page_shift;

    for (int level = 1; level >= 0; --level) {
        const PhysAddr pte_address =
            table_address +
            static_cast<PhysAddr>(
                vpn[static_cast<unsigned int>(level)]) *
                4U;
        const ReadResult read_result = bus.read(
            pte_address,
            AccessWidth::Word,
            AccessKind::PageTableWalk);
        if (!read_result.ok()) {
            return access_fault(read_result.fault);
        }

        std::uint32_t pte =
            static_cast<std::uint32_t>(read_result.value);
        const bool valid =
            (pte & sv32_pte_bits::valid) != 0U;
        const bool readable =
            (pte & sv32_pte_bits::read) != 0U;
        const bool writable =
            (pte & sv32_pte_bits::write) != 0U;
        const bool executable =
            (pte & sv32_pte_bits::execute) != 0U;

        if (!valid || (!readable && writable)) {
            return page_fault();
        }

        if (!readable && !executable) {
            // U, A, and D are reserved in a non-leaf Sv32 PTE.
            if ((pte & (sv32_pte_bits::user |
                        sv32_pte_bits::accessed |
                        sv32_pte_bits::dirty)) != 0U) {
                return page_fault();
            }
            if (level == 0) {
                return page_fault();
            }
            table_address =
                static_cast<PhysAddr>(pte >> 10U)
                << page_shift;
            continue;
        }

        const std::uint32_t pte_ppn = pte >> 10U;
        if (level == 1 &&
            (pte_ppn & pte_ppn0_mask) != 0U) {
            return page_fault();
        }
        if (!privilege_allows(
                pte,
                privilege,
                access,
                state.machine_csrs.mstatus)) {
            return page_fault();
        }

        std::uint32_t updated_pte =
            pte | sv32_pte_bits::accessed;
        if (access == MemoryAccessType::Store) {
            updated_pte |= sv32_pte_bits::dirty;
        }
        if (updated_pte != pte) {
            // Core and device execution is serialized in this single-hart
            // emulator, making this full-width PTE update indivisible with
            // respect to all other emulated bus users.
            const BusFault write_fault = bus.write(
                pte_address,
                AccessWidth::Word,
                updated_pte,
                AccessKind::PageTableWalk);
            if (write_fault != BusFault::None) {
                return access_fault(write_fault);
            }
        }

        const PhysAddr offset =
            static_cast<PhysAddr>(
                virtual_address & page_offset_mask);
        if (level == 1) {
            const PhysAddr physical_page =
                static_cast<PhysAddr>(
                    pte_ppn & ~pte_ppn0_mask)
                    << page_shift;
            const PhysAddr superpage_offset =
                static_cast<PhysAddr>(vpn[0])
                << page_shift;
            return ready(
                physical_page | superpage_offset | offset);
        }

        return ready(
            (static_cast<PhysAddr>(pte_ppn) << page_shift) |
            offset);
    }

    return page_fault();
}

} // namespace rv32
