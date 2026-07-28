#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>

#include "rv32/core/core.hpp"
#include "rv32/core/csr.hpp"
#include "rv32/core/decode.hpp"
#include "rv32/core/execute.hpp"
#include "rv32/core/frontend.hpp"
#include "rv32/core/mmu.hpp"
#include "rv32/core/trap.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

class MemoryBus final : public rv32::CpuBus {
  public:
    std::optional<rv32::PhysAddr> read_fault_address;
    std::optional<rv32::PhysAddr> write_fault_address;
    rv32::BusFault injected_read_fault{rv32::BusFault::Unmapped};
    rv32::BusFault injected_write_fault{rv32::BusFault::ReadOnly};
    std::uint32_t page_walk_reads{};
    std::uint32_t page_walk_writes{};
    std::uint32_t instruction_reads{};
    std::uint32_t load_reads{};
    rv32::AccessKind last_write_kind{
        rv32::AccessKind::InstructionFetch};
    rv32::PhysAddr last_write_address{};
    std::uint64_t last_write_value{};

    void store_word(rv32::PhysAddr address, std::uint32_t value)
    {
        for (unsigned int index = 0; index < 4U; ++index) {
            bytes_[address + index] = static_cast<std::uint8_t>(
                value >> (8U * index));
        }
    }

    [[nodiscard]] std::uint32_t load_word(
        rv32::PhysAddr address) const
    {
        std::uint32_t value = 0;
        for (unsigned int index = 0; index < 4U; ++index) {
            const auto found = bytes_.find(address + index);
            if (found == bytes_.end()) {
                return 0;
            }
            value |= static_cast<std::uint32_t>(found->second)
                     << (8U * index);
        }
        return value;
    }

    void clear_counters() noexcept
    {
        page_walk_reads = 0;
        page_walk_writes = 0;
        instruction_reads = 0;
        load_reads = 0;
        last_write_kind = rv32::AccessKind::InstructionFetch;
        last_write_address = 0;
        last_write_value = 0;
    }

    rv32::ReadResult read(
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        rv32::AccessKind kind) override
    {
        if (kind == rv32::AccessKind::PageTableWalk) {
            ++page_walk_reads;
        } else if (kind == rv32::AccessKind::InstructionFetch) {
            ++instruction_reads;
        } else if (kind == rv32::AccessKind::Load) {
            ++load_reads;
        }

        if (read_fault_address.has_value() &&
            address == *read_fault_address) {
            return {.fault = injected_read_fault};
        }

        const auto count =
            static_cast<rv32::PhysAddr>(rv32::width_bytes(width));
        if ((address & (count - 1U)) != 0U) {
            return {.fault = rv32::BusFault::Misaligned};
        }

        std::uint64_t value = 0;
        for (rv32::PhysAddr index = 0; index < count; ++index) {
            const auto found = bytes_.find(address + index);
            if (found == bytes_.end()) {
                return {.fault = rv32::BusFault::Unmapped};
            }
            value |= static_cast<std::uint64_t>(found->second)
                     << (8U * static_cast<unsigned int>(index));
        }
        return {
            .fault = rv32::BusFault::None,
            .value = value,
        };
    }

    rv32::BusFault write(
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        std::uint64_t value,
        rv32::AccessKind kind) override
    {
        if (kind == rv32::AccessKind::PageTableWalk) {
            ++page_walk_writes;
        }
        last_write_kind = kind;
        last_write_address = address;
        last_write_value = value;

        if (write_fault_address.has_value() &&
            address == *write_fault_address) {
            return injected_write_fault;
        }

        const auto count =
            static_cast<rv32::PhysAddr>(rv32::width_bytes(width));
        if ((address & (count - 1U)) != 0U) {
            return rv32::BusFault::Misaligned;
        }
        for (rv32::PhysAddr index = 0; index < count; ++index) {
            bytes_[address + index] = static_cast<std::uint8_t>(
                value >> (8U * static_cast<unsigned int>(index)));
        }
        ++write_epoch_;
        return rv32::BusFault::None;
    }

    rv32::ReadResult load_reserved_word(
        std::uint32_t hart_id,
        rv32::PhysAddr address) override
    {
        const auto result =
            read(address, rv32::AccessWidth::Word, rv32::AccessKind::Atomic);
        if (result.ok()) {
            reservation_hart_ = hart_id;
            reservation_address_ = address;
            reservation_epoch_ = write_epoch_;
        }
        return result;
    }

    rv32::StoreConditionalResult store_conditional_word(
        std::uint32_t hart_id,
        rv32::PhysAddr address,
        std::uint32_t value) override
    {
        const bool matches =
            reservation_hart_.has_value() &&
            *reservation_hart_ == hart_id &&
            reservation_address_ == address &&
            reservation_epoch_ == write_epoch_;
        reservation_hart_.reset();
        if (!matches) {
            return {
                .fault = rv32::BusFault::None,
                .succeeded = false,
            };
        }
        const auto fault = write(
            address,
            rv32::AccessWidth::Word,
            value,
            rv32::AccessKind::Atomic);
        return {
            .fault = fault,
            .succeeded = fault == rv32::BusFault::None,
        };
    }

    rv32::AtomicResult atomic_word(
        std::uint32_t,
        rv32::PhysAddr address,
        rv32::AmoOperation operation,
        std::uint32_t operand) override
    {
        const auto read_result =
            read(address, rv32::AccessWidth::Word, rv32::AccessKind::Atomic);
        if (!read_result.ok()) {
            return {.fault = read_result.fault};
        }
        const auto original =
            static_cast<std::uint32_t>(read_result.value);
        std::uint32_t replacement = operand;
        switch (operation) {
        case rv32::AmoOperation::Swap:
            break;
        case rv32::AmoOperation::Add:
            replacement = original + operand;
            break;
        case rv32::AmoOperation::Xor:
            replacement = original ^ operand;
            break;
        case rv32::AmoOperation::And:
            replacement = original & operand;
            break;
        case rv32::AmoOperation::Or:
            replacement = original | operand;
            break;
        case rv32::AmoOperation::Min:
            replacement =
                static_cast<std::int32_t>(original) <
                        static_cast<std::int32_t>(operand)
                    ? original
                    : operand;
            break;
        case rv32::AmoOperation::Max:
            replacement =
                static_cast<std::int32_t>(original) >
                        static_cast<std::int32_t>(operand)
                    ? original
                    : operand;
            break;
        case rv32::AmoOperation::MinUnsigned:
            replacement = std::min(original, operand);
            break;
        case rv32::AmoOperation::MaxUnsigned:
            replacement = std::max(original, operand);
            break;
        }
        const auto fault = write(
            address,
            rv32::AccessWidth::Word,
            replacement,
            rv32::AccessKind::Atomic);
        return {
            .fault = fault,
            .original_value = original,
        };
    }

    std::uint64_t read_time() const noexcept override
    {
        return 0;
    }

  private:
    std::map<rv32::PhysAddr, std::uint8_t> bytes_;
    std::optional<std::uint32_t> reservation_hart_;
    rv32::PhysAddr reservation_address_{};
    std::uint64_t reservation_epoch_{};
    std::uint64_t write_epoch_{};
};

constexpr rv32::PhysAddr root_address = 0x1000U;
constexpr rv32::PhysAddr leaf_table_address = 0x2000U;

struct Mapping {
    rv32::PhysAddr root_pte{};
    rv32::PhysAddr leaf_pte{};
};

[[nodiscard]] constexpr std::uint32_t make_pte(
    rv32::PhysAddr physical_address,
    std::uint32_t flags) noexcept
{
    const auto ppn = static_cast<std::uint32_t>(
        (physical_address >> 12U) & rv32::satp_bits::ppn);
    return (ppn << 10U) | flags;
}

Mapping map_4k(
    MemoryBus& bus,
    rv32::CpuSnapshot& state,
    std::uint32_t virtual_address,
    rv32::PhysAddr physical_address,
    std::uint32_t flags)
{
    const std::uint32_t vpn1 =
        (virtual_address >> 22U) & 0x3FFU;
    const std::uint32_t vpn0 =
        (virtual_address >> 12U) & 0x3FFU;
    const rv32::PhysAddr root_pte =
        root_address + static_cast<rv32::PhysAddr>(vpn1) * 4U;
    const rv32::PhysAddr leaf_pte =
        leaf_table_address +
        static_cast<rv32::PhysAddr>(vpn0) * 4U;

    bus.store_word(
        root_pte,
        make_pte(
            leaf_table_address,
            rv32::sv32_pte_bits::valid));
    bus.store_word(
        leaf_pte,
        make_pte(
            physical_address,
            flags | rv32::sv32_pte_bits::valid));
    state.supervisor_csrs.satp =
        rv32::satp_bits::mode |
        static_cast<std::uint32_t>(root_address >> 12U);
    return {
        .root_pte = root_pte,
        .leaf_pte = leaf_pte,
    };
}

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd,
    std::uint32_t opcode) noexcept
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd) noexcept
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           0x37U;
}

[[nodiscard]] constexpr std::uint32_t encode_csr(
    rv32::CsrAddress address,
    std::uint32_t source) noexcept
{
    return encode_i(address, source, 0x1U, 0U, 0x73U);
}

void test_bare_and_machine_bypass()
{
    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;

    const auto bare = rv32::translate_address(
        bus,
        state,
        0xFEDCBA98U,
        rv32::MemoryAccessType::Load);
    CHECK(bare.ready());
    CHECK(bare.physical_address == 0xFEDCBA98ULL);
    CHECK(bus.page_walk_reads == 0U);

    state.privilege = rv32::PrivilegeMode::Machine;
    state.supervisor_csrs.satp =
        rv32::satp_bits::mode | 0x3FFFFFU;
    const auto machine = rv32::translate_address(
        bus,
        state,
        0x12345678U,
        rv32::MemoryAccessType::InstructionFetch);
    CHECK(machine.ready());
    CHECK(machine.physical_address == 0x12345678ULL);
    CHECK(bus.page_walk_reads == 0U);
}

void test_base_page_and_megapage_translation()
{
    constexpr std::uint32_t virtual_address = 0x80403124U;
    constexpr rv32::PhysAddr physical_page = 0x008AB000ULL;
    constexpr std::uint32_t leaf_flags =
        rv32::sv32_pte_bits::read |
        rv32::sv32_pte_bits::accessed;

    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    map_4k(
        bus,
        state,
        virtual_address,
        physical_page,
        leaf_flags);

    const auto base_page = rv32::translate_address(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(base_page.ready());
    CHECK(base_page.physical_address == physical_page + 0x124U);
    CHECK(bus.page_walk_reads == 2U);
    CHECK(bus.page_walk_writes == 0U);

    MemoryBus super_bus;
    rv32::CpuSnapshot super_state;
    super_state.privilege = rv32::PrivilegeMode::Supervisor;
    super_state.supervisor_csrs.satp =
        rv32::satp_bits::mode |
        static_cast<std::uint32_t>(root_address >> 12U);
    constexpr std::uint32_t super_virtual = 0x81234568U;
    constexpr rv32::PhysAddr super_physical = 0x100000000ULL;
    const auto root_pte =
        root_address +
        static_cast<rv32::PhysAddr>(super_virtual >> 22U) * 4U;
    super_bus.store_word(
        root_pte,
        make_pte(
            super_physical,
            rv32::sv32_pte_bits::valid |
                rv32::sv32_pte_bits::read |
                rv32::sv32_pte_bits::execute |
                rv32::sv32_pte_bits::accessed));

    const auto megapage = rv32::translate_address(
        super_bus,
        super_state,
        super_virtual,
        rv32::MemoryAccessType::InstructionFetch);
    CHECK(megapage.ready());
    CHECK(
        megapage.physical_address ==
        super_physical + (super_virtual & 0x003FFFFFU));
    CHECK(megapage.physical_address > 0xFFFFFFFFULL);
    CHECK(super_bus.page_walk_reads == 1U);

    super_bus.store_word(
        root_pte,
        make_pte(
            super_physical + 0x1000U,
            rv32::sv32_pte_bits::valid |
                rv32::sv32_pte_bits::read |
                rv32::sv32_pte_bits::accessed));
    const auto misaligned = rv32::translate_address(
        super_bus,
        super_state,
        super_virtual,
        rv32::MemoryAccessType::Load);
    CHECK(misaligned.status == rv32::TranslationStatus::PageFault);
    CHECK(misaligned.bus_fault == rv32::BusFault::None);
}

void test_invalid_page_table_entries()
{
    constexpr std::uint32_t virtual_address = 0x40403000U;
    const auto root_pte =
        root_address +
        static_cast<rv32::PhysAddr>(virtual_address >> 22U) * 4U;

    {
        MemoryBus bus;
        rv32::CpuSnapshot state;
        state.privilege = rv32::PrivilegeMode::Supervisor;
        state.supervisor_csrs.satp =
            rv32::satp_bits::mode |
            static_cast<std::uint32_t>(root_address >> 12U);
        bus.store_word(root_pte, 0U);
        CHECK(
            rv32::translate_address(
                bus,
                state,
                virtual_address,
                rv32::MemoryAccessType::Load)
                .status == rv32::TranslationStatus::PageFault);
    }

    {
        MemoryBus bus;
        rv32::CpuSnapshot state;
        state.privilege = rv32::PrivilegeMode::Supervisor;
        state.supervisor_csrs.satp =
            rv32::satp_bits::mode |
            static_cast<std::uint32_t>(root_address >> 12U);
        bus.store_word(
            root_pte,
            rv32::sv32_pte_bits::valid |
                rv32::sv32_pte_bits::write);
        CHECK(
            rv32::translate_address(
                bus,
                state,
                virtual_address,
                rv32::MemoryAccessType::Store)
                .status == rv32::TranslationStatus::PageFault);
    }

    {
        MemoryBus bus;
        rv32::CpuSnapshot state;
        state.privilege = rv32::PrivilegeMode::Supervisor;
        const auto mapping = map_4k(
            bus,
            state,
            virtual_address,
            0x9000U,
            0U);
        CHECK(
            rv32::translate_address(
                bus,
                state,
                virtual_address,
                rv32::MemoryAccessType::Load)
                .status == rv32::TranslationStatus::PageFault);

        bus.store_word(
            mapping.root_pte,
            make_pte(
                leaf_table_address,
                rv32::sv32_pte_bits::valid |
                    rv32::sv32_pte_bits::user));
        CHECK(
            rv32::translate_address(
                bus,
                state,
                virtual_address,
                rv32::MemoryAccessType::Load)
                .status == rv32::TranslationStatus::PageFault);
    }
}

void test_sum_mxr_and_user_permissions()
{
    constexpr std::uint32_t virtual_address = 0x50001000U;
    constexpr rv32::PhysAddr physical_address = 0xA000U;
    constexpr std::uint32_t user_rwx =
        rv32::sv32_pte_bits::user |
        rv32::sv32_pte_bits::read |
        rv32::sv32_pte_bits::write |
        rv32::sv32_pte_bits::execute |
        rv32::sv32_pte_bits::accessed |
        rv32::sv32_pte_bits::dirty;

    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    const auto mapping = map_4k(
        bus,
        state,
        virtual_address,
        physical_address,
        user_rwx);

    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::Load)
            .status == rv32::TranslationStatus::PageFault);
    state.machine_csrs.mstatus = rv32::mstatus_bits::sum;
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::Load)
            .ready());
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::Store)
            .ready());
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::InstructionFetch)
            .status == rv32::TranslationStatus::PageFault);

    state.privilege = rv32::PrivilegeMode::User;
    state.machine_csrs.mstatus = 0;
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::InstructionFetch)
            .ready());

    bus.store_word(
        mapping.leaf_pte,
        make_pte(
            physical_address,
            rv32::sv32_pte_bits::valid |
                rv32::sv32_pte_bits::execute |
                rv32::sv32_pte_bits::user |
                rv32::sv32_pte_bits::accessed));
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::Load)
            .status == rv32::TranslationStatus::PageFault);
    state.machine_csrs.mstatus = rv32::mstatus_bits::mxr;
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::Load)
            .ready());

    state.machine_csrs.mstatus = 0;
    bus.store_word(
        mapping.leaf_pte,
        make_pte(
            physical_address,
            rv32::sv32_pte_bits::valid |
                rv32::sv32_pte_bits::read |
                rv32::sv32_pte_bits::accessed));
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::Load)
            .status == rv32::TranslationStatus::PageFault);
    state.privilege = rv32::PrivilegeMode::Supervisor;
    CHECK(
        rv32::translate_address(
            bus,
            state,
            virtual_address,
            rv32::MemoryAccessType::Store)
            .status == rv32::TranslationStatus::PageFault);
}

void test_accessed_dirty_updates_and_walk_faults()
{
    constexpr std::uint32_t virtual_address = 0x60002000U;
    constexpr rv32::PhysAddr physical_address = 0xB000U;
    constexpr std::uint32_t rw =
        rv32::sv32_pte_bits::read |
        rv32::sv32_pte_bits::write;

    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    const auto mapping = map_4k(
        bus,
        state,
        virtual_address,
        physical_address,
        rw);

    const auto load = rv32::translate_address(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(load.ready());
    CHECK(bus.page_walk_writes == 1U);
    CHECK(bus.last_write_kind == rv32::AccessKind::PageTableWalk);
    CHECK(bus.last_write_address == mapping.leaf_pte);
    CHECK(
        (bus.load_word(mapping.leaf_pte) &
         rv32::sv32_pte_bits::accessed) != 0U);
    CHECK(
        (bus.load_word(mapping.leaf_pte) &
         rv32::sv32_pte_bits::dirty) == 0U);

    bus.store_word(
        mapping.leaf_pte,
        make_pte(
            physical_address,
            rw | rv32::sv32_pte_bits::valid));
    bus.clear_counters();
    const auto store = rv32::translate_address(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Store);
    CHECK(store.ready());
    CHECK(bus.page_walk_writes == 1U);
    CHECK(
        (bus.load_word(mapping.leaf_pte) &
         (rv32::sv32_pte_bits::accessed |
          rv32::sv32_pte_bits::dirty)) ==
        (rv32::sv32_pte_bits::accessed |
         rv32::sv32_pte_bits::dirty));

    bus.read_fault_address = mapping.root_pte;
    const auto read_fault = rv32::translate_address(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(read_fault.status == rv32::TranslationStatus::AccessFault);
    CHECK(read_fault.bus_fault == rv32::BusFault::Unmapped);
    bus.read_fault_address.reset();

    bus.store_word(
        mapping.leaf_pte,
        make_pte(
            physical_address,
            rw | rv32::sv32_pte_bits::valid));
    bus.write_fault_address = mapping.leaf_pte;
    const auto write_fault = rv32::translate_address(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Store);
    CHECK(write_fault.status == rv32::TranslationStatus::AccessFault);
    CHECK(write_fault.bus_fault == rv32::BusFault::ReadOnly);
}

void test_tlb_hits_asids_global_entries_and_fences()
{
    constexpr std::uint32_t virtual_address = 0x62003124U;
    constexpr rv32::PhysAddr physical_a = 0x0000C000ULL;
    constexpr rv32::PhysAddr physical_b = 0x0000D000ULL;
    constexpr rv32::PhysAddr physical_c = 0x0000E000ULL;
    constexpr std::uint32_t read_write =
        rv32::sv32_pte_bits::read |
        rv32::sv32_pte_bits::write |
        rv32::sv32_pte_bits::accessed;

    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    const auto mapping = map_4k(
        bus,
        state,
        virtual_address,
        physical_a,
        read_write);
    state.supervisor_csrs.satp |= 1U << 22U;

    rv32::Sv32Tlb tlb;
    rv32::MmuPerformanceCounters counters;
    const auto first = tlb.translate(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load,
        &counters);
    CHECK(first.ready());
    CHECK(first.physical_address == physical_a + 0x124U);
    CHECK(counters.tlb_misses == 1U);
    CHECK(counters.page_table_walks == 1U);
    CHECK(counters.pte_reads == 2U);
    CHECK(tlb.valid_entries() == 1U);

    bus.clear_counters();
    const auto hit = tlb.translate(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load,
        &counters);
    CHECK(hit.ready());
    CHECK(hit.physical_address == first.physical_address);
    CHECK(bus.page_walk_reads == 0U);
    CHECK(counters.tlb_hits == 1U);

    // A store cannot use a load-filled entry whose cached PTE lacks D. The
    // fresh walk must set D before the store translation is cached.
    const auto store = tlb.translate(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Store,
        &counters);
    CHECK(store.ready());
    CHECK(bus.page_walk_reads == 2U);
    CHECK(
        (bus.load_word(mapping.leaf_pte) &
         rv32::sv32_pte_bits::dirty) != 0U);

    bus.store_word(
        mapping.leaf_pte,
        make_pte(
            physical_b,
            read_write |
                rv32::sv32_pte_bits::dirty |
                rv32::sv32_pte_bits::valid));
    const auto stale = tlb.translate(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(stale.physical_address == physical_a + 0x124U);

    tlb.sfence_vma(virtual_address, std::nullopt);
    const auto refreshed = tlb.translate(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(refreshed.physical_address == physical_b + 0x124U);

    // The same virtual page for another ASID coexists in the set.
    bus.store_word(
        mapping.leaf_pte,
        make_pte(
            physical_c,
            read_write |
                rv32::sv32_pte_bits::dirty |
                rv32::sv32_pte_bits::valid));
    state.supervisor_csrs.satp =
        (state.supervisor_csrs.satp & ~rv32::satp_bits::asid) |
        (2U << 22U);
    const auto asid_two = tlb.translate(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(asid_two.physical_address == physical_c + 0x124U);

    state.supervisor_csrs.satp =
        (state.supervisor_csrs.satp & ~rv32::satp_bits::asid) |
        (1U << 22U);
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv32::MemoryAccessType::Load)
            .physical_address ==
        physical_b + 0x124U);
    tlb.sfence_vma(
        std::nullopt,
        std::optional<std::uint16_t>{1U});
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv32::MemoryAccessType::Load)
            .physical_address ==
        physical_c + 0x124U);

    // Global entries match every ASID and survive ASID-scoped fences.
    bus.store_word(
        mapping.leaf_pte,
        make_pte(
            physical_a,
            read_write |
                rv32::sv32_pte_bits::dirty |
                rv32::sv32_pte_bits::global |
                rv32::sv32_pte_bits::valid));
    tlb.sfence_vma(virtual_address, std::nullopt);
    const auto global = tlb.translate(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(global.physical_address == physical_a + 0x124U);
    tlb.sfence_vma(
        std::nullopt,
        std::optional<std::uint16_t>{1U});
    state.supervisor_csrs.satp =
        (state.supervisor_csrs.satp & ~rv32::satp_bits::asid) |
        (3U << 22U);
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv32::MemoryAccessType::Load)
            .physical_address ==
        physical_a + 0x124U);
    tlb.sfence_vma(std::nullopt, std::nullopt);
    CHECK(tlb.valid_entries() == 0U);
}

void test_mprv_effective_privilege()
{
    constexpr std::uint32_t virtual_address = 0x70003000U;
    constexpr rv32::PhysAddr physical_address = 0xC000U;

    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Machine;
    map_4k(
        bus,
        state,
        virtual_address,
        physical_address,
        rv32::sv32_pte_bits::read |
            rv32::sv32_pte_bits::accessed);
    state.machine_csrs.mstatus =
        rv32::mstatus_bits::mprv |
        (static_cast<std::uint32_t>(
             rv32::PrivilegeMode::Supervisor)
         << rv32::mstatus_bits::mpp_shift);

    const auto load = rv32::translate_address(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::Load);
    CHECK(load.ready());
    CHECK(load.physical_address == physical_address);
    CHECK(
        rv32::effective_privilege(
            state,
            rv32::MemoryAccessType::Load) ==
        rv32::PrivilegeMode::Supervisor);

    const auto reads_before_fetch = bus.page_walk_reads;
    const auto fetch = rv32::translate_address(
        bus,
        state,
        virtual_address,
        rv32::MemoryAccessType::InstructionFetch);
    CHECK(fetch.ready());
    CHECK(fetch.physical_address == virtual_address);
    CHECK(bus.page_walk_reads == reads_before_fetch);
}

void test_fault_classification_paths()
{
    constexpr std::uint32_t virtual_address = 0x30004000U;
    const auto root_pte =
        root_address +
        static_cast<rv32::PhysAddr>(virtual_address >> 22U) * 4U;

    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    state.supervisor_csrs.satp =
        rv32::satp_bits::mode |
        static_cast<std::uint32_t>(root_address >> 12U);
    bus.store_word(root_pte, 0U);

    const auto instruction_page_fault =
        rv32::fetch_decode(bus, virtual_address, &state);
    CHECK(
        instruction_page_fault.status ==
        rv32::FrontendStatus::InstructionPageFault);
    CHECK(instruction_page_fault.bus_fault == rv32::BusFault::None);
    CHECK(instruction_page_fault.trap_value == virtual_address);

    bus.read_fault_address = root_pte;
    const auto instruction_access_fault =
        rv32::fetch_decode(bus, virtual_address, &state);
    CHECK(
        instruction_access_fault.status ==
        rv32::FrontendStatus::InstructionAccessFault);
    CHECK(
        instruction_access_fault.bus_fault ==
        rv32::BusFault::Unmapped);
    CHECK(instruction_access_fault.trap_value == virtual_address);
    bus.read_fault_address.reset();

    const auto mapping = map_4k(
        bus,
        state,
        virtual_address,
        0xD000U,
        rv32::sv32_pte_bits::read |
            rv32::sv32_pte_bits::accessed);
    const rv32::DecodedInstruction store{
        .kind = rv32::InstructionKind::Sw,
        .raw = 0x0020A023U,
        .rs1 = 1U,
        .rs2 = 2U,
    };
    const auto store_page_fault = rv32::execute_memory(
        bus,
        store,
        0x80000000U,
        virtual_address,
        0x12345678U,
        &state);
    CHECK(
        store_page_fault.status ==
        rv32::MemoryStatus::StorePageFault);
    CHECK(store_page_fault.bus_fault == rv32::BusFault::None);
    CHECK(store_page_fault.trap_value == virtual_address);

    bus.store_word(mapping.leaf_pte, 0U);
    const rv32::DecodedInstruction load{
        .kind = rv32::InstructionKind::Lw,
        .raw = 0x0000A283U,
        .rd = 5U,
        .rs1 = 1U,
    };
    const auto load_page_fault = rv32::execute_memory(
        bus,
        load,
        0x80000000U,
        virtual_address,
        0,
        &state);
    CHECK(
        load_page_fault.status ==
        rv32::MemoryStatus::LoadPageFault);
    CHECK(load_page_fault.bus_fault == rv32::BusFault::None);

    bus.read_fault_address = mapping.root_pte;
    const auto load_access_fault = rv32::execute_memory(
        bus,
        load,
        0x80000000U,
        virtual_address,
        0,
        &state);
    CHECK(
        load_access_fault.status ==
        rv32::MemoryStatus::LoadAccessFault);
    CHECK(
        load_access_fault.bus_fault ==
        rv32::BusFault::Unmapped);

    rv32::CpuSnapshot delegated_state;
    delegated_state.pc = 0x40000000U;
    delegated_state.privilege = rv32::PrivilegeMode::User;
    delegated_state.machine_csrs.medeleg = 1U << 13U;
    delegated_state.supervisor_csrs.stvec = 0x80000100U;
    const auto target = rv32::take_trap(
        delegated_state,
        {
            .cause = rv32::ExceptionCause::LoadPageFault,
            .exception_pc = delegated_state.pc,
            .trap_value = virtual_address,
        });
    CHECK(target == rv32::TrapTarget::Supervisor);
    CHECK(delegated_state.privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(
        delegated_state.supervisor_csrs.scause ==
        static_cast<std::uint32_t>(
            rv32::ExceptionCause::LoadPageFault));
    CHECK(delegated_state.supervisor_csrs.stval == virtual_address);
}

void test_frontend_memory_and_atomic_translation()
{
    constexpr std::uint32_t code_virtual = 0x40000000U;
    constexpr rv32::PhysAddr code_physical = 0x3000U;
    constexpr std::uint32_t data_virtual = 0x40001000U;
    constexpr rv32::PhysAddr data_physical = 0x4000U;

    MemoryBus bus;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    map_4k(
        bus,
        state,
        code_virtual,
        code_physical,
        rv32::sv32_pte_bits::read |
            rv32::sv32_pte_bits::execute |
            rv32::sv32_pte_bits::accessed);
    const auto data_mapping = map_4k(
        bus,
        state,
        data_virtual,
        data_physical,
        rv32::sv32_pte_bits::read |
            rv32::sv32_pte_bits::write |
            rv32::sv32_pte_bits::accessed |
            rv32::sv32_pte_bits::dirty);
    bus.store_word(code_physical, encode_i(7U, 0U, 0U, 1U, 0x13U));
    bus.store_word(data_physical, 0x11223344U);

    const auto frontend =
        rv32::fetch_decode(bus, code_virtual, &state);
    CHECK(frontend.ready());
    CHECK(frontend.decoded.kind == rv32::InstructionKind::Addi);
    CHECK(bus.instruction_reads == 2U);

    const rv32::DecodedInstruction load{
        .kind = rv32::InstructionKind::Lw,
        .raw = encode_i(0U, 1U, 0x2U, 5U, 0x03U),
        .rd = 5U,
        .rs1 = 1U,
    };
    const auto loaded = rv32::execute_memory(
        bus,
        load,
        code_virtual,
        data_virtual,
        0,
        &state);
    CHECK(loaded.ready());
    CHECK(loaded.pending.register_write.value == 0x11223344U);

    const rv32::DecodedInstruction lr{
        .kind = rv32::InstructionKind::LrW,
        .raw = 0x1000A2AFU,
        .rd = 5U,
        .rs1 = 1U,
    };
    const auto reserved = rv32::execute_atomic(
        bus,
        lr,
        code_virtual,
        0,
        data_virtual,
        0,
        &state);
    CHECK(reserved.ready());
    CHECK(reserved.pending.register_write.value == 0x11223344U);

    const rv32::DecodedInstruction sc{
        .kind = rv32::InstructionKind::ScW,
        .raw = 0x1820A32FU,
        .rd = 6U,
        .rs1 = 1U,
        .rs2 = 2U,
    };
    const auto conditional = rv32::execute_atomic(
        bus,
        sc,
        code_virtual,
        0,
        data_virtual,
        0x55667788U,
        &state);
    CHECK(conditional.ready());
    CHECK(conditional.pending.register_write.value == 0U);
    CHECK(bus.load_word(data_physical) == 0x55667788U);

    bus.store_word(
        data_mapping.leaf_pte,
        make_pte(
            data_physical,
            rv32::sv32_pte_bits::valid |
                rv32::sv32_pte_bits::read |
                rv32::sv32_pte_bits::accessed));
    const rv32::DecodedInstruction amo{
        .kind = rv32::InstructionKind::AmoSwapW,
        .raw = 0x0820A2AFU,
        .rd = 5U,
        .rs1 = 1U,
        .rs2 = 2U,
    };
    const auto denied = rv32::execute_atomic(
        bus,
        amo,
        code_virtual,
        0,
        data_virtual,
        1U,
        &state);
    CHECK(denied.status == rv32::AtomicStatus::StorePageFault);
    CHECK(denied.bus_fault == rv32::BusFault::None);
}

void test_satp_tvm_sfence_and_mprv_return()
{
    CHECK(rv32::sanitize_satp(0x7FFFFFFFU) == 0U);
    CHECK(
        rv32::sanitize_satp(0xFFFFFFFFU) ==
        (rv32::satp_bits::mode |
         rv32::satp_bits::asid |
         rv32::satp_bits::ppn));
    CHECK(
        (rv32::supported_exception_delegation &
         ((1U << 12U) | (1U << 13U) | (1U << 15U))) ==
        ((1U << 12U) | (1U << 13U) | (1U << 15U)));

    MemoryBus bus;
    rv32::CpuSnapshot state;
    rv32::CsrFile csrs(state, bus);
    csrs.write_validated(rv32::csr_address::satp, 0xFFFFFFFFU);
    CHECK(
        csrs.read(
                rv32::csr_address::satp,
                rv32::PrivilegeMode::Supervisor)
            .value ==
        (rv32::satp_bits::mode |
         rv32::satp_bits::asid |
         rv32::satp_bits::ppn));

    state.machine_csrs.mstatus = rv32::mstatus_bits::tvm;
    CHECK(
        csrs.read(
                rv32::csr_address::satp,
                rv32::PrivilegeMode::Supervisor)
            .status == rv32::CsrAccessStatus::PrivilegeViolation);
    CHECK(
        csrs.validate_write(
            rv32::csr_address::satp,
            rv32::PrivilegeMode::Supervisor) ==
        rv32::CsrAccessStatus::PrivilegeViolation);
    CHECK(
        csrs.read(
                rv32::csr_address::satp,
                rv32::PrivilegeMode::Machine)
            .ready());

    state.machine_csrs.mstatus =
        rv32::mstatus_bits::sum |
        rv32::mstatus_bits::mxr |
        rv32::mstatus_bits::mprv |
        rv32::mstatus_bits::tvm;
    const auto sstatus = csrs.read(
        rv32::csr_address::sstatus,
        rv32::PrivilegeMode::Supervisor);
    CHECK(sstatus.ready());
    CHECK(
        sstatus.value ==
        (rv32::mstatus_bits::sum | rv32::mstatus_bits::mxr));
    csrs.write_validated(rv32::csr_address::sstatus, 0U);
    CHECK(
        (state.machine_csrs.mstatus &
         (rv32::mstatus_bits::sum | rv32::mstatus_bits::mxr)) ==
        0U);
    CHECK(
        (state.machine_csrs.mstatus &
         (rv32::mstatus_bits::mprv | rv32::mstatus_bits::tvm)) ==
        (rv32::mstatus_bits::mprv | rv32::mstatus_bits::tvm));

    const auto sfence =
        rv32::decode_instruction(0x12730073U);
    CHECK(sfence.kind == rv32::InstructionKind::SfenceVma);
    state.pc = 0x80000000U;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    CHECK(
        rv32::execute_privileged(sfence, state).status ==
        rv32::PrivilegedExecutionStatus::IllegalInstruction);
    state.machine_csrs.mstatus = 0;
    CHECK(rv32::execute_privileged(sfence, state).ready());
    state.privilege = rv32::PrivilegeMode::User;
    CHECK(
        rv32::execute_privileged(sfence, state).status ==
        rv32::PrivilegedExecutionStatus::IllegalInstruction);
    state.privilege = rv32::PrivilegeMode::Machine;
    CHECK(rv32::execute_privileged(sfence, state).ready());

    state.machine_csrs.mstatus =
        rv32::mstatus_bits::mprv |
        rv32::mstatus_bits::mpie |
        (static_cast<std::uint32_t>(
             rv32::PrivilegeMode::Supervisor)
         << rv32::mstatus_bits::mpp_shift);
    state.machine_csrs.mepc = 0x80000100U;
    const auto mret =
        rv32::execute_privileged(
            rv32::decode_instruction(0x30200073U),
            state);
    CHECK(mret.ready());
    CHECK(rv32::commit_pending(state, mret.pending, &csrs));
    CHECK(state.privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(
        (state.machine_csrs.mstatus & rv32::mstatus_bits::mprv) ==
        0U);
}

void test_core_reports_load_page_fault()
{
    constexpr std::uint32_t boot_pc = 0x80000000U;
    constexpr std::uint32_t virtual_code = 0x40000000U;
    constexpr rv32::PhysAddr physical_code = 0x3000U;

    MemoryBus bus;
    rv32::CpuSnapshot page_state;
    page_state.privilege = rv32::PrivilegeMode::Supervisor;
    map_4k(
        bus,
        page_state,
        virtual_code,
        physical_code,
        rv32::sv32_pte_bits::read |
            rv32::sv32_pte_bits::execute |
            rv32::sv32_pte_bits::accessed);
    bus.store_word(root_address, 0U);

    const std::uint32_t program[]{
        encode_u(0x80000000U, 1U),
        encode_i(1U, 1U, 0U, 1U, 0x13U),
        encode_csr(rv32::csr_address::satp, 1U),
        encode_u(virtual_code, 2U),
        encode_csr(rv32::csr_address::mepc, 2U),
        encode_u(0x1000U, 3U),
        encode_i(0x800U, 3U, 0U, 3U, 0x13U),
        encode_csr(rv32::csr_address::mstatus, 3U),
        0x30200073U,
    };
    for (std::size_t index = 0;
         index < std::size(program);
         ++index) {
        bus.store_word(
            boot_pc + static_cast<rv32::PhysAddr>(index) * 4U,
            program[index]);
    }
    bus.store_word(
        physical_code,
        encode_i(0U, 0U, 0x2U, 5U, 0x03U));

    rv32::Core core(bus);
    core.reset({
        .reset_pc = boot_pc,
        .hart_id = 0,
        .initial_privilege = rv32::PrivilegeMode::Machine,
    });
    for (std::size_t index = 0;
         index < std::size(program);
         ++index) {
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
    }
    CHECK(core.snapshot().privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(core.snapshot().pc == virtual_code);

    const auto result = core.step({});
    const auto trapped = core.snapshot();
    CHECK(result.status == rv32::StepStatus::TrapTaken);
    CHECK(result.pc == virtual_code);
    CHECK(result.trap_value == 0U);
    CHECK(result.bus_fault == rv32::BusFault::None);
    CHECK(trapped.privilege == rv32::PrivilegeMode::Machine);
    CHECK(trapped.machine_csrs.mepc == virtual_code);
    CHECK(
        trapped.machine_csrs.mcause ==
        static_cast<std::uint32_t>(
            rv32::ExceptionCause::LoadPageFault));
    CHECK(trapped.machine_csrs.mtval == 0U);
    CHECK(trapped.instructions_retired == std::size(program));
}

void test_core_sfence_vma_invalidates_local_tlb()
{
    constexpr std::uint32_t boot_pc = 0x80000000U;
    constexpr std::uint32_t virtual_code = 0x40400000U;
    constexpr rv32::PhysAddr physical_code = 0x00003000U;

    MemoryBus bus;
    rv32::CpuSnapshot page_state;
    page_state.privilege = rv32::PrivilegeMode::Supervisor;
    map_4k(
        bus,
        page_state,
        virtual_code,
        physical_code,
        rv32::sv32_pte_bits::read |
            rv32::sv32_pte_bits::execute |
            rv32::sv32_pte_bits::accessed);

    const std::uint32_t program[]{
        encode_u(0x80000000U, 1U),
        encode_i(1U, 1U, 0U, 1U, 0x13U),
        encode_csr(rv32::csr_address::satp, 1U),
        encode_u(virtual_code, 2U),
        encode_csr(rv32::csr_address::mepc, 2U),
        encode_u(0x1000U, 3U),
        encode_i(0x800U, 3U, 0U, 3U, 0x13U),
        encode_csr(rv32::csr_address::mstatus, 3U),
        0x30200073U,
    };
    for (std::size_t index = 0;
         index < std::size(program);
         ++index) {
        bus.store_word(
            boot_pc + static_cast<rv32::PhysAddr>(index) * 4U,
            program[index]);
    }
    bus.store_word(physical_code, 0x12000073U);

    rv32::Core core(bus);
    core.reset({
        .reset_pc = boot_pc,
        .hart_id = 0,
        .initial_privilege = rv32::PrivilegeMode::Machine,
    });
    for (std::size_t index = 0;
         index < std::size(program);
         ++index) {
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
    }
    CHECK(core.snapshot().privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(core.snapshot().pc == virtual_code);
    CHECK(core.tlb_entries() == 0U);

    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.snapshot().pc == virtual_code + 4U);
    CHECK(core.tlb_entries() == 0U);
    CHECK(core.performance_counters().mmu.tlb_misses >= 1U);
}

} // namespace

int main()
{
    test_bare_and_machine_bypass();
    test_base_page_and_megapage_translation();
    test_invalid_page_table_entries();
    test_sum_mxr_and_user_permissions();
    test_accessed_dirty_updates_and_walk_faults();
    test_tlb_hits_asids_global_entries_and_fences();
    test_mprv_effective_privilege();
    test_fault_classification_paths();
    test_frontend_memory_and_atomic_translation();
    test_satp_tvm_sfence_and_mprv_return();
    test_core_reports_load_page_fault();
    test_core_sfence_vma_invalidates_local_tlb();

    if (failures != 0) {
        std::cerr << failures << " MMU test(s) failed\n";
        return 1;
    }
    return 0;
}
