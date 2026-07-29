#include "rv64/core/mmu.hpp"

#include <algorithm>

#include "rv64/core/csr.hpp"

namespace rv64 {

namespace {

constexpr unsigned int base_page_shift = 12U;
constexpr unsigned int vpn_width = 9U;
constexpr Xlen vpn_mask = 0x1FFU;
constexpr Xlen page_offset_mask = 0xFFFU;

struct WalkMetadata {
    Xlen pte{};
    std::uint8_t page_shift{};
    bool global{};
};

[[nodiscard]] constexpr TranslationResult ready(
    rv::PhysAddr physical_address) noexcept
{
    return {
        .status = TranslationStatus::Ready,
        .physical_address = physical_address,
        .bus_fault = rv::BusFault::None,
    };
}

[[nodiscard]] constexpr TranslationResult page_fault() noexcept
{
    return {
        .status = TranslationStatus::PageFault,
        .physical_address = 0,
        .bus_fault = rv::BusFault::None,
    };
}

[[nodiscard]] constexpr TranslationResult access_fault(
    rv::BusFault fault) noexcept
{
    return {
        .status = TranslationStatus::AccessFault,
        .physical_address = 0,
        .bus_fault = fault,
    };
}

[[nodiscard]] constexpr bool canonical_sv39_address(
    Xlen address) noexcept
{
    const Xlen upper = address >> 39U;
    const bool sign = ((address >> 38U) & 1U) != 0U;
    return sign ? upper == ((Xlen{1} << 25U) - 1U)
                : upper == 0U;
}

[[nodiscard]] constexpr bool privilege_allows(
    Xlen pte,
    PrivilegeMode privilege,
    MemoryAccessType access,
    Xlen mstatus) noexcept
{
    const bool user_page =
        (pte & sv39_pte_bits::user) != 0U;
    if (privilege == PrivilegeMode::User) {
        if (!user_page) {
            return false;
        }
    } else if (
        privilege == PrivilegeMode::Supervisor &&
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
        return (pte & sv39_pte_bits::execute) != 0U;
    case MemoryAccessType::Load:
        return
            (pte & sv39_pte_bits::read) != 0U ||
            ((mstatus & mstatus_bits::mxr) != 0U &&
             (pte & sv39_pte_bits::execute) != 0U);
    case MemoryAccessType::Store:
        return (pte & sv39_pte_bits::write) != 0U;
    }
    return false;
}

[[nodiscard]] constexpr Xlen offset_mask(
    std::uint8_t shift) noexcept
{
    return (Xlen{1} << shift) - 1U;
}

[[nodiscard]] TranslationResult walk_address(
    rv::CpuBus& bus,
    const CpuSnapshot& state,
    Xlen virtual_address,
    MemoryAccessType access,
    WalkMetadata* metadata,
    MmuPerformanceCounters* counters)
{
    const Xlen satp = sanitize_satp(
        state.supervisor_csrs.satp);
    const PrivilegeMode privilege =
        effective_privilege(state, access);
    const Xlen vpn[3]{
        (virtual_address >> 12U) & vpn_mask,
        (virtual_address >> 21U) & vpn_mask,
        (virtual_address >> 30U) & vpn_mask,
    };
    rv::PhysAddr table_address =
        static_cast<rv::PhysAddr>(satp & satp_bits::ppn)
        << base_page_shift;
    bool inherited_global = false;

    for (int level = 2; level >= 0; --level) {
        const rv::PhysAddr pte_address =
            table_address +
            static_cast<rv::PhysAddr>(
                vpn[static_cast<unsigned int>(level)]) *
                8U;
        if (counters != nullptr) {
            ++counters->pte_reads;
        }
        const rv::ReadResult read_result = bus.read(
            pte_address,
            rv::AccessWidth::DoubleWord,
            rv::AccessKind::PageTableWalk);
        if (!read_result.ok()) {
            if (counters != nullptr) {
                ++counters->access_faults;
            }
            return access_fault(read_result.fault);
        }

        Xlen pte = read_result.value;
        const bool valid =
            (pte & sv39_pte_bits::valid) != 0U;
        const bool readable =
            (pte & sv39_pte_bits::read) != 0U;
        const bool writable =
            (pte & sv39_pte_bits::write) != 0U;
        const bool executable =
            (pte & sv39_pte_bits::execute) != 0U;

        if (!valid || (!readable && writable) ||
            (pte & sv39_pte_bits::reserved) != 0U) {
            if (counters != nullptr) {
                ++counters->page_faults;
            }
            return page_fault();
        }

        if (!readable && !executable) {
            if ((pte & (sv39_pte_bits::user |
                        sv39_pte_bits::accessed |
                        sv39_pte_bits::dirty)) != 0U ||
                level == 0) {
                if (counters != nullptr) {
                    ++counters->page_faults;
                }
                return page_fault();
            }
            inherited_global =
                inherited_global ||
                (pte & sv39_pte_bits::global) != 0U;
            table_address =
                static_cast<rv::PhysAddr>(
                    (pte & sv39_pte_bits::ppn) >> 10U)
                << base_page_shift;
            continue;
        }

        const Xlen pte_ppn =
            (pte & sv39_pte_bits::ppn) >> 10U;
        const unsigned int lower_ppn_bits =
            static_cast<unsigned int>(level) * vpn_width;
        const Xlen lower_ppn_mask =
            lower_ppn_bits == 0U
                ? 0U
                : (Xlen{1} << lower_ppn_bits) - 1U;
        if ((pte_ppn & lower_ppn_mask) != 0U ||
            !privilege_allows(
                pte,
                privilege,
                access,
                state.machine_csrs.mstatus)) {
            if (counters != nullptr) {
                ++counters->page_faults;
            }
            return page_fault();
        }

        Xlen updated_pte =
            pte | sv39_pte_bits::accessed;
        if (access == MemoryAccessType::Store) {
            updated_pte |= sv39_pte_bits::dirty;
        }
        if (updated_pte != pte) {
            if (counters != nullptr) {
                ++counters->pte_writes;
            }
            const rv::BusFault write_fault = bus.write(
                pte_address,
                rv::AccessWidth::DoubleWord,
                updated_pte,
                rv::AccessKind::PageTableWalk);
            if (write_fault != rv::BusFault::None) {
                if (counters != nullptr) {
                    ++counters->access_faults;
                }
                return access_fault(write_fault);
            }
            pte = updated_pte;
        }

        const std::uint8_t leaf_shift =
            static_cast<std::uint8_t>(
                base_page_shift +
                static_cast<unsigned int>(level) * vpn_width);
        const Xlen lower_vpn =
            (virtual_address >> base_page_shift) &
            lower_ppn_mask;
        const rv::PhysAddr physical_address =
            (static_cast<rv::PhysAddr>(
                 (pte_ppn & ~lower_ppn_mask) |
                 lower_vpn)
             << base_page_shift) |
            static_cast<rv::PhysAddr>(
                virtual_address & page_offset_mask);
        if (metadata != nullptr) {
            *metadata = {
                .pte = pte,
                .page_shift = leaf_shift,
                .global =
                    inherited_global ||
                    (pte & sv39_pte_bits::global) != 0U,
            };
        }
        return ready(physical_address);
    }

    if (counters != nullptr) {
        ++counters->page_faults;
    }
    return page_fault();
}

[[nodiscard]] constexpr std::size_t set_for(
    Xlen virtual_page) noexcept
{
    return static_cast<std::size_t>(
        virtual_page ^
        (virtual_page >> 6U) ^
        (virtual_page >> 12U)) %
        Sv39Tlb::set_count;
}

} // namespace

Xlen sanitize_satp(Xlen value) noexcept
{
    if ((value & satp_bits::mode) !=
        satp_bits::sv39_mode) {
        return 0;
    }
    return
        satp_bits::sv39_mode |
        (value & (satp_bits::asid | satp_bits::ppn));
}

PrivilegeMode effective_privilege(
    const CpuSnapshot& state,
    MemoryAccessType access) noexcept
{
    const Xlen mstatus =
        sanitize_mstatus(state.machine_csrs.mstatus);
    if (access == MemoryAccessType::InstructionFetch ||
        (mstatus & mstatus_bits::mprv) == 0U) {
        return state.privilege;
    }

    const Xlen encoded =
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
    rv::CpuBus& bus,
    const CpuSnapshot& state,
    Xlen virtual_address,
    MemoryAccessType access)
{
    const PrivilegeMode privilege =
        effective_privilege(state, access);
    const Xlen satp = sanitize_satp(
        state.supervisor_csrs.satp);
    if (privilege == PrivilegeMode::Machine ||
        (satp & satp_bits::mode) == 0U) {
        return ready(
            static_cast<rv::PhysAddr>(virtual_address));
    }
    if (!canonical_sv39_address(virtual_address)) {
        return page_fault();
    }

    return walk_address(
        bus,
        state,
        virtual_address,
        access,
        nullptr,
        nullptr);
}

const Sv39Tlb::Entry* Sv39Tlb::lookup(
    const CpuSnapshot& state,
    Xlen virtual_address,
    MemoryAccessType access) const noexcept
{
    const Xlen satp = sanitize_satp(
        state.supervisor_csrs.satp);
    const std::uint16_t asid =
        static_cast<std::uint16_t>(
            (satp & satp_bits::asid) >> 44U);
    const Xlen root_ppn = satp & satp_bits::ppn;
    const PrivilegeMode privilege =
        effective_privilege(state, access);

    const auto find_at_shift =
        [&](std::uint8_t shift) -> const Entry* {
        const Xlen virtual_page =
            virtual_address >> shift;
        const std::size_t first =
            set_for(virtual_page) * ways;
        for (std::size_t way = 0; way < ways; ++way) {
            const Entry& entry = entries_[first + way];
            if (!entry.valid ||
                entry.page_shift != shift ||
                entry.virtual_page != virtual_page) {
                continue;
            }
            if (!entry.global &&
                (entry.asid != asid ||
                 entry.root_ppn != root_ppn)) {
                continue;
            }
            if (!privilege_allows(
                    entry.pte,
                    privilege,
                    access,
                    state.machine_csrs.mstatus)) {
                continue;
            }
            if ((entry.pte & sv39_pte_bits::accessed) == 0U ||
                (access == MemoryAccessType::Store &&
                 (entry.pte & sv39_pte_bits::dirty) == 0U)) {
                continue;
            }
            return &entry;
        }
        return nullptr;
    };

    for (const std::uint8_t shift :
         {std::uint8_t{12}, std::uint8_t{21}, std::uint8_t{30}}) {
        if (const Entry* entry = find_at_shift(shift);
            entry != nullptr) {
            return entry;
        }
    }
    return nullptr;
}

void Sv39Tlb::insert(
    const CpuSnapshot& state,
    Xlen virtual_address,
    rv::PhysAddr physical_address,
    Xlen pte,
    std::uint8_t page_shift,
    bool global) noexcept
{
    const Xlen satp = sanitize_satp(
        state.supervisor_csrs.satp);
    const Xlen virtual_page =
        virtual_address >> page_shift;
    const std::size_t set = set_for(virtual_page);
    const std::size_t first = set * ways;
    std::size_t index = first;
    bool found_slot = false;
    const std::uint16_t asid =
        static_cast<std::uint16_t>(
            (satp & satp_bits::asid) >> 44U);
    const Xlen root_ppn = satp & satp_bits::ppn;
    for (std::size_t way = 0; way < ways; ++way) {
        const Entry& candidate = entries_[first + way];
        if (!candidate.valid ||
            (candidate.page_shift == page_shift &&
             candidate.virtual_page == virtual_page &&
             candidate.global == global &&
             (global ||
              (candidate.asid == asid &&
               candidate.root_ppn == root_ppn)))) {
            index = first + way;
            found_slot = true;
            break;
        }
    }
    if (!found_slot) {
        const std::size_t replacement =
            replacement_ways_[set] % ways;
        index = first + replacement;
        replacement_ways_[set] =
            static_cast<std::uint8_t>(
                (replacement + 1U) % ways);
    }
    const Xlen mask = offset_mask(page_shift);
    entries_[index] = {
        .valid = true,
        .global = global,
        .page_shift = page_shift,
        .asid = asid,
        .root_ppn = root_ppn,
        .virtual_page = virtual_page,
        .physical_page =
            physical_address &
            ~static_cast<rv::PhysAddr>(mask),
        .pte = pte,
    };
}

TranslationResult Sv39Tlb::translate(
    rv::CpuBus& bus,
    const CpuSnapshot& state,
    Xlen virtual_address,
    MemoryAccessType access,
    MmuPerformanceCounters* counters)
{
    if (counters != nullptr) {
        ++counters->translations;
    }
    const PrivilegeMode privilege =
        effective_privilege(state, access);
    const Xlen satp = sanitize_satp(
        state.supervisor_csrs.satp);
    if (privilege == PrivilegeMode::Machine ||
        (satp & satp_bits::mode) == 0U) {
        if (counters != nullptr) {
            ++counters->bare_translations;
        }
        return ready(
            static_cast<rv::PhysAddr>(virtual_address));
    }
    if (!canonical_sv39_address(virtual_address)) {
        if (counters != nullptr) {
            ++counters->page_faults;
        }
        return page_fault();
    }

    if (const Entry* entry =
            lookup(state, virtual_address, access);
        entry != nullptr) {
        if (counters != nullptr) {
            ++counters->tlb_hits;
        }
        const rv::PhysAddr offset =
            static_cast<rv::PhysAddr>(
                virtual_address &
                offset_mask(entry->page_shift));
        return ready(entry->physical_page | offset);
    }

    if (counters != nullptr) {
        ++counters->tlb_misses;
        ++counters->page_table_walks;
    }
    WalkMetadata metadata;
    const TranslationResult result = walk_address(
        bus,
        state,
        virtual_address,
        access,
        &metadata,
        counters);
    if (result.ready()) {
        insert(
            state,
            virtual_address,
            result.physical_address,
            metadata.pte,
            metadata.page_shift,
            metadata.global);
    }
    return result;
}

void Sv39Tlb::clear() noexcept
{
    entries_ = {};
    replacement_ways_ = {};
}

void Sv39Tlb::sfence_vma(
    std::optional<Xlen> virtual_address,
    std::optional<std::uint16_t> asid) noexcept
{
    for (Entry& entry : entries_) {
        if (!entry.valid) {
            continue;
        }
        if (asid.has_value() &&
            (entry.global || entry.asid != *asid)) {
            continue;
        }
        if (virtual_address.has_value() &&
            (*virtual_address >> entry.page_shift) !=
                entry.virtual_page) {
            continue;
        }
        entry.valid = false;
    }
}

std::size_t Sv39Tlb::valid_entries() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        entries_.begin(),
        entries_.end(),
        [](const Entry& entry) {
            return entry.valid;
        }));
}

} // namespace rv64
