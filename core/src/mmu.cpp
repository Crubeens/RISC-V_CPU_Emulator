#include "rv32/core/mmu.hpp"

#include <algorithm>

#include "rv32/core/csr.hpp"

namespace rv32 {

namespace {

constexpr std::uint32_t page_shift = 12U;
constexpr std::uint32_t megapage_shift = 22U;
constexpr std::uint32_t vpn_mask = 0x3FFU;
constexpr std::uint32_t page_offset_mask = 0xFFFU;
constexpr std::uint32_t pte_ppn0_mask = 0x3FFU;

struct WalkMetadata {
    std::uint32_t pte{};
    std::uint8_t page_shift{};
    bool global{};
};

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

[[nodiscard]] constexpr std::uint32_t address_mask(
    std::uint8_t shift) noexcept
{
    return shift == megapage_shift
               ? 0x003FFFFFU
               : page_offset_mask;
}

[[nodiscard]] TranslationResult walk_address(
    CpuBus& bus,
    const CpuSnapshot& state,
    std::uint32_t virtual_address,
    MemoryAccessType access,
    WalkMetadata* metadata,
    MmuPerformanceCounters* counters)
{
    const std::uint32_t satp =
        sanitize_satp(state.supervisor_csrs.satp);
    const PrivilegeMode privilege =
        effective_privilege(state, access);
    const std::uint32_t vpn[2]{
        (virtual_address >> 12U) & vpn_mask,
        (virtual_address >> 22U) & vpn_mask,
    };
    PhysAddr table_address =
        static_cast<PhysAddr>(satp & satp_bits::ppn)
        << page_shift;
    bool inherited_global = false;

    for (int level = 1; level >= 0; --level) {
        const PhysAddr pte_address =
            table_address +
            static_cast<PhysAddr>(
                vpn[static_cast<unsigned int>(level)]) *
                4U;
        if (counters != nullptr) {
            ++counters->pte_reads;
        }
        const ReadResult read_result = bus.read(
            pte_address,
            AccessWidth::Word,
            AccessKind::PageTableWalk);
        if (!read_result.ok()) {
            if (counters != nullptr) {
                ++counters->access_faults;
            }
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
            if (counters != nullptr) {
                ++counters->page_faults;
            }
            return page_fault();
        }

        if (!readable && !executable) {
            // U, A, and D are reserved in a non-leaf Sv32 PTE.
            if ((pte & (sv32_pte_bits::user |
                        sv32_pte_bits::accessed |
                        sv32_pte_bits::dirty)) != 0U ||
                level == 0) {
                if (counters != nullptr) {
                    ++counters->page_faults;
                }
                return page_fault();
            }
            inherited_global =
                inherited_global ||
                (pte & sv32_pte_bits::global) != 0U;
            table_address =
                static_cast<PhysAddr>(pte >> 10U)
                << page_shift;
            continue;
        }

        const std::uint32_t pte_ppn = pte >> 10U;
        if ((level == 1 &&
             (pte_ppn & pte_ppn0_mask) != 0U) ||
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

        std::uint32_t updated_pte =
            pte | sv32_pte_bits::accessed;
        if (access == MemoryAccessType::Store) {
            updated_pte |= sv32_pte_bits::dirty;
        }
        if (updated_pte != pte) {
            if (counters != nullptr) {
                ++counters->pte_writes;
            }
            // Core and device execution is serialized in this single-hart
            // emulator, making this full-width PTE update indivisible with
            // respect to all other emulated bus users.
            const BusFault write_fault = bus.write(
                pte_address,
                AccessWidth::Word,
                updated_pte,
                AccessKind::PageTableWalk);
            if (write_fault != BusFault::None) {
                if (counters != nullptr) {
                    ++counters->access_faults;
                }
                return access_fault(write_fault);
            }
            pte = updated_pte;
        }

        const PhysAddr offset =
            static_cast<PhysAddr>(
                virtual_address & page_offset_mask);
        PhysAddr physical_address{};
        std::uint8_t leaf_shift{};
        if (level == 1) {
            const PhysAddr physical_page =
                static_cast<PhysAddr>(
                    pte_ppn & ~pte_ppn0_mask)
                    << page_shift;
            const PhysAddr superpage_offset =
                static_cast<PhysAddr>(vpn[0])
                << page_shift;
            physical_address =
                physical_page | superpage_offset | offset;
            leaf_shift =
                static_cast<std::uint8_t>(megapage_shift);
        } else {
            physical_address =
                (static_cast<PhysAddr>(pte_ppn) << page_shift) |
                offset;
            leaf_shift = static_cast<std::uint8_t>(page_shift);
        }
        if (metadata != nullptr) {
            *metadata = {
                .pte = pte,
                .page_shift = leaf_shift,
                .global =
                    inherited_global ||
                    (pte & sv32_pte_bits::global) != 0U,
            };
        }
        return ready(physical_address);
    }

    if (counters != nullptr) {
        ++counters->page_faults;
    }
    return page_fault();
}

} // namespace

std::uint32_t sanitize_satp(std::uint32_t value) noexcept
{
    if ((value & satp_bits::mode) == 0U) {
        return 0;
    }
    return
        satp_bits::mode |
        (value & (satp_bits::asid | satp_bits::ppn));
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

    return walk_address(
        bus,
        state,
        virtual_address,
        access,
        nullptr,
        nullptr);
}

const Sv32Tlb::Entry* Sv32Tlb::lookup(
    const CpuSnapshot& state,
    std::uint32_t virtual_address,
    MemoryAccessType access) const noexcept
{
    const std::uint32_t satp =
        sanitize_satp(state.supervisor_csrs.satp);
    const std::uint16_t asid = static_cast<std::uint16_t>(
        (satp & satp_bits::asid) >> 22U);
    const std::uint32_t root_ppn = satp & satp_bits::ppn;
    const PrivilegeMode privilege =
        effective_privilege(state, access);

    const auto find_in_set =
        [&](std::uint8_t leaf_page_shift) -> const Entry* {
        const std::uint32_t virtual_page =
            virtual_address >> leaf_page_shift;
        const std::size_t set =
            static_cast<std::size_t>(
                virtual_page ^
                (virtual_page >> 6U) ^
                (virtual_page >> 12U)) %
            set_count;
        const std::size_t first = set * ways;
        for (std::size_t way = 0; way < ways; ++way) {
            const Entry& entry = entries_[first + way];
            if (!entry.valid ||
                entry.page_shift != leaf_page_shift ||
                virtual_page != entry.virtual_page) {
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
            if ((entry.pte & sv32_pte_bits::accessed) == 0U ||
                (access == MemoryAccessType::Store &&
                 (entry.pte & sv32_pte_bits::dirty) == 0U)) {
                continue;
            }
            return &entry;
        }
        return nullptr;
    };

    if (const Entry* entry = find_in_set(
            static_cast<std::uint8_t>(page_shift));
        entry != nullptr) {
        return entry;
    }
    if (const Entry* entry = find_in_set(
            static_cast<std::uint8_t>(megapage_shift));
        entry != nullptr) {
        return entry;
    }
    return nullptr;
}

void Sv32Tlb::insert(
    const CpuSnapshot& state,
    std::uint32_t virtual_address,
    PhysAddr physical_address,
    std::uint32_t pte,
    std::uint8_t leaf_page_shift,
    bool global) noexcept
{
    const std::uint32_t satp =
        sanitize_satp(state.supervisor_csrs.satp);
    const std::uint32_t virtual_page =
        virtual_address >> leaf_page_shift;
    const std::size_t set =
        static_cast<std::size_t>(
            virtual_page ^
            (virtual_page >> 6U) ^
            (virtual_page >> 12U)) %
        set_count;
    const std::size_t first = set * ways;
    std::size_t index = first;
    bool found_slot = false;
    const std::uint16_t entry_asid =
        static_cast<std::uint16_t>(
            (satp & satp_bits::asid) >> 22U);
    const std::uint32_t root_ppn = satp & satp_bits::ppn;
    for (std::size_t way = 0; way < ways; ++way) {
        const Entry& candidate = entries_[first + way];
        if (!candidate.valid ||
            (candidate.page_shift == leaf_page_shift &&
             candidate.virtual_page == virtual_page &&
             candidate.global == global &&
             (global ||
              (candidate.asid == entry_asid &&
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
        replacement_ways_[set] = static_cast<std::uint8_t>(
            (replacement + 1U) % ways);
    }
    const std::uint32_t mask = address_mask(leaf_page_shift);
    entries_[index] = {
        .valid = true,
        .global = global,
        .page_shift = leaf_page_shift,
        .asid = entry_asid,
        .root_ppn = root_ppn,
        .virtual_page = virtual_page,
        .physical_page =
            physical_address & ~static_cast<PhysAddr>(mask),
        .pte = pte,
    };
}

TranslationResult Sv32Tlb::translate(
    CpuBus& bus,
    const CpuSnapshot& state,
    std::uint32_t virtual_address,
    MemoryAccessType access,
    MmuPerformanceCounters* counters)
{
    if (counters != nullptr) {
        ++counters->translations;
    }
    const PrivilegeMode privilege =
        effective_privilege(state, access);
    const std::uint32_t satp =
        sanitize_satp(state.supervisor_csrs.satp);
    if (privilege == PrivilegeMode::Machine ||
        (satp & satp_bits::mode) == 0U) {
        if (counters != nullptr) {
            ++counters->bare_translations;
        }
        return ready(static_cast<PhysAddr>(virtual_address));
    }

    if (const Entry* entry =
            lookup(state, virtual_address, access);
        entry != nullptr) {
        if (counters != nullptr) {
            ++counters->tlb_hits;
        }
        const PhysAddr offset =
            static_cast<PhysAddr>(
                virtual_address &
                address_mask(entry->page_shift));
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

void Sv32Tlb::clear() noexcept
{
    entries_ = {};
    replacement_ways_ = {};
}

void Sv32Tlb::sfence_vma(
    std::optional<std::uint32_t> virtual_address,
    std::optional<std::uint16_t> asid) noexcept
{
    for (auto& entry : entries_) {
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

std::size_t Sv32Tlb::valid_entries() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        entries_.begin(),
        entries_.end(),
        [](const Entry& entry) {
            return entry.valid;
        }));
}

} // namespace rv32
