#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>

#include "rv/common/bus.hpp"
#include "rv64/core/core.hpp"
#include "rv64/core/csr.hpp"
#include "rv64/core/mmu.hpp"
#include "rv64/core/trap.hpp"

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

class MemoryBus final : public rv::CpuBus {
  public:
    std::optional<rv::PhysAddr> read_fault_address;
    std::optional<rv::PhysAddr> write_fault_address;
    rv::BusFault injected_read_fault{rv::BusFault::Unmapped};
    rv::BusFault injected_write_fault{rv::BusFault::ReadOnly};
    std::uint32_t page_walk_reads{};
    std::uint32_t page_walk_writes{};

    void store(
        rv::PhysAddr address,
        rv::AccessWidth width,
        std::uint64_t value)
    {
        const auto count =
            static_cast<rv::PhysAddr>(rv::width_bytes(width));
        for (rv::PhysAddr index = 0; index < count; ++index) {
            bytes_[address + index] = static_cast<std::uint8_t>(
                value >> (8U * static_cast<unsigned int>(index)));
        }
    }

    void store_word(rv::PhysAddr address, std::uint32_t value)
    {
        store(address, rv::AccessWidth::Word, value);
    }

    void store_doubleword(
        rv::PhysAddr address,
        std::uint64_t value)
    {
        store(address, rv::AccessWidth::DoubleWord, value);
    }

    [[nodiscard]] std::uint64_t load_doubleword(
        rv::PhysAddr address) const
    {
        std::uint64_t value = 0;
        for (unsigned int index = 0; index < 8U; ++index) {
            const auto found = bytes_.find(address + index);
            if (found == bytes_.end()) {
                return 0;
            }
            value |= static_cast<std::uint64_t>(found->second)
                     << (8U * index);
        }
        return value;
    }

    [[nodiscard]] rv::ReadResult read(
        rv::PhysAddr address,
        rv::AccessWidth width,
        rv::AccessKind kind) override
    {
        if (kind == rv::AccessKind::PageTableWalk) {
            ++page_walk_reads;
        }
        if (read_fault_address.has_value() &&
            address == *read_fault_address) {
            return {.fault = injected_read_fault};
        }
        const auto count =
            static_cast<rv::PhysAddr>(rv::width_bytes(width));
        if ((address & (count - 1U)) != 0U) {
            return {.fault = rv::BusFault::Misaligned};
        }
        std::uint64_t value = 0;
        for (rv::PhysAddr index = 0; index < count; ++index) {
            const auto found = bytes_.find(address + index);
            if (found == bytes_.end()) {
                return {.fault = rv::BusFault::Unmapped};
            }
            value |= static_cast<std::uint64_t>(found->second)
                     << (8U * static_cast<unsigned int>(index));
        }
        return {
            .fault = rv::BusFault::None,
            .value = value,
        };
    }

    [[nodiscard]] rv::BusFault write(
        rv::PhysAddr address,
        rv::AccessWidth width,
        std::uint64_t value,
        rv::AccessKind kind) override
    {
        if (kind == rv::AccessKind::PageTableWalk) {
            ++page_walk_writes;
        }
        if (write_fault_address.has_value() &&
            address == *write_fault_address) {
            return injected_write_fault;
        }
        const auto count =
            static_cast<rv::PhysAddr>(rv::width_bytes(width));
        if ((address & (count - 1U)) != 0U) {
            return rv::BusFault::Misaligned;
        }
        store(address, width, value);
        ++write_epoch_;
        return rv::BusFault::None;
    }

    [[nodiscard]] rv::ReadResult load_reserved_word(
        std::uint32_t,
        rv::PhysAddr address) override
    {
        return read(
            address,
            rv::AccessWidth::Word,
            rv::AccessKind::Atomic);
    }

    [[nodiscard]] rv::StoreConditionalResult store_conditional_word(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        std::uint32_t value) override
    {
        const auto result = store_conditional(
            hart_id,
            address,
            rv::AccessWidth::Word,
            value);
        return result;
    }

    [[nodiscard]] rv::AtomicResult atomic_word(
        std::uint32_t,
        rv::PhysAddr,
        rv::AmoOperation,
        std::uint32_t) override
    {
        return {.fault = rv::BusFault::Unsupported};
    }

    [[nodiscard]] rv::ReadResult load_reserved_doubleword(
        std::uint32_t hart_id,
        rv::PhysAddr address) override
    {
        const rv::ReadResult result = read(
            address,
            rv::AccessWidth::DoubleWord,
            rv::AccessKind::Atomic);
        if (result.ok()) {
            reservation_hart_ = hart_id;
            reservation_address_ = address;
            reservation_epoch_ = write_epoch_;
        }
        return result;
    }

    [[nodiscard]] rv::StoreConditionalResult
    store_conditional_doubleword(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        std::uint64_t value) override
    {
        return store_conditional(
            hart_id,
            address,
            rv::AccessWidth::DoubleWord,
            value);
    }

    [[nodiscard]] std::uint64_t read_time() const noexcept override
    {
        return 0;
    }

  private:
    [[nodiscard]] rv::StoreConditionalResult store_conditional(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        rv::AccessWidth width,
        std::uint64_t value)
    {
        const bool matches =
            reservation_hart_.has_value() &&
            *reservation_hart_ == hart_id &&
            reservation_address_ == address &&
            reservation_epoch_ == write_epoch_;
        reservation_hart_.reset();
        if (!matches) {
            return {
                .fault = rv::BusFault::None,
                .succeeded = false,
            };
        }
        const rv::BusFault fault = write(
            address,
            width,
            value,
            rv::AccessKind::Atomic);
        return {
            .fault = fault,
            .succeeded = fault == rv::BusFault::None,
        };
    }

    std::map<rv::PhysAddr, std::uint8_t> bytes_;
    std::optional<std::uint32_t> reservation_hart_;
    rv::PhysAddr reservation_address_{};
    std::uint64_t reservation_epoch_{};
    std::uint64_t write_epoch_{};
};

constexpr rv::PhysAddr root_address = 0x1000U;
constexpr rv::PhysAddr level_one_address = 0x2000U;
constexpr rv::PhysAddr level_zero_address = 0x3000U;

struct Mapping {
    rv::PhysAddr root_pte{};
    rv::PhysAddr level_one_pte{};
    rv::PhysAddr leaf_pte{};
};

[[nodiscard]] constexpr std::uint64_t make_pte(
    rv::PhysAddr physical_address,
    std::uint64_t flags) noexcept
{
    return
        ((physical_address >> 12U) << 10U) |
        flags;
}

void enable_sv39(
    rv64::CpuSnapshot& state,
    std::uint16_t asid = 0)
{
    state.supervisor_csrs.satp =
        rv64::satp_bits::sv39_mode |
        (static_cast<std::uint64_t>(asid) << 44U) |
        (root_address >> 12U);
}

Mapping map_4k(
    MemoryBus& bus,
    rv64::CpuSnapshot& state,
    std::uint64_t virtual_address,
    rv::PhysAddr physical_address,
    std::uint64_t flags)
{
    const std::uint64_t vpn2 =
        (virtual_address >> 30U) & 0x1FFU;
    const std::uint64_t vpn1 =
        (virtual_address >> 21U) & 0x1FFU;
    const std::uint64_t vpn0 =
        (virtual_address >> 12U) & 0x1FFU;
    const rv::PhysAddr root_pte =
        root_address + vpn2 * 8U;
    const rv::PhysAddr level_one_pte =
        level_one_address + vpn1 * 8U;
    const rv::PhysAddr leaf_pte =
        level_zero_address + vpn0 * 8U;

    bus.store_doubleword(
        root_pte,
        make_pte(
            level_one_address,
            rv64::sv39_pte_bits::valid));
    bus.store_doubleword(
        level_one_pte,
        make_pte(
            level_zero_address,
            rv64::sv39_pte_bits::valid));
    bus.store_doubleword(
        leaf_pte,
        make_pte(
            physical_address,
            flags | rv64::sv39_pte_bits::valid));
    enable_sv39(state);
    return {root_pte, level_one_pte, leaf_pte};
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
           opcode;
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
    rv64::CsrAddress address,
    std::uint32_t source) noexcept
{
    return (static_cast<std::uint32_t>(address) << 20U) |
           ((source & 0x1FU) << 15U) |
           (1U << 12U) |
           0x73U;
}

void test_satp_bare_machine_and_canonical_addresses()
{
    CHECK(rv64::sanitize_satp(0x7FFFFFFFFFFFFFFFULL) == 0U);
    CHECK(
        rv64::sanitize_satp(
            rv64::satp_bits::sv39_mode |
            rv64::satp_bits::asid |
            rv64::satp_bits::ppn) ==
        (rv64::satp_bits::sv39_mode |
         rv64::satp_bits::asid |
         rv64::satp_bits::ppn));

    MemoryBus bus;
    rv64::CpuSnapshot state;
    rv64::CsrFile csrs(state, bus);
    csrs.write_validated(
        rv64::csr_address::satp,
        rv64::satp_bits::sv39_mode |
            rv64::satp_bits::asid |
            rv64::satp_bits::ppn);
    CHECK(
        state.supervisor_csrs.satp ==
        (rv64::satp_bits::sv39_mode |
         rv64::satp_bits::asid |
         rv64::satp_bits::ppn));
    csrs.write_validated(
        rv64::csr_address::satp,
        rv64::satp_bits::asid |
            rv64::satp_bits::ppn);
    CHECK(state.supervisor_csrs.satp == 0U);

    state.privilege = rv64::PrivilegeMode::Supervisor;
    auto result = rv64::translate_address(
        bus,
        state,
        0xFEDCBA9876543210ULL,
        rv64::MemoryAccessType::Load);
    CHECK(result.ready());
    CHECK(result.physical_address == 0xFEDCBA9876543210ULL);

    state.privilege = rv64::PrivilegeMode::Machine;
    enable_sv39(state);
    result = rv64::translate_address(
        bus,
        state,
        0x0000004000000000ULL,
        rv64::MemoryAccessType::InstructionFetch);
    CHECK(result.ready());
    CHECK(result.physical_address == 0x0000004000000000ULL);

    state.privilege = rv64::PrivilegeMode::Supervisor;
    result = rv64::translate_address(
        bus,
        state,
        0x0000004000000000ULL,
        rv64::MemoryAccessType::Load);
    CHECK(result.status == rv64::TranslationStatus::PageFault);
    CHECK(bus.page_walk_reads == 0U);
}

void test_base_and_superpage_translation()
{
    constexpr std::uint64_t virtual_address = 0x0000001234567123ULL;
    constexpr rv::PhysAddr physical_page = 0x00000000008AB000ULL;
    constexpr std::uint64_t rx =
        rv64::sv39_pte_bits::read |
        rv64::sv39_pte_bits::execute |
        rv64::sv39_pte_bits::accessed;

    MemoryBus bus;
    rv64::CpuSnapshot state;
    state.privilege = rv64::PrivilegeMode::Supervisor;
    map_4k(
        bus,
        state,
        virtual_address,
        physical_page,
        rx);
    auto result = rv64::translate_address(
        bus,
        state,
        virtual_address,
        rv64::MemoryAccessType::InstructionFetch);
    CHECK(result.ready());
    CHECK(result.physical_address == physical_page + 0x123U);
    CHECK(bus.page_walk_reads == 3U);

    MemoryBus page_2m_bus;
    rv64::CpuSnapshot page_2m_state;
    page_2m_state.privilege = rv64::PrivilegeMode::Supervisor;
    enable_sv39(page_2m_state);
    constexpr std::uint64_t virtual_2m = 0x0000000123456123ULL;
    constexpr rv::PhysAddr physical_2m = 0x0000000002200000ULL;
    const auto vpn2 = (virtual_2m >> 30U) & 0x1FFU;
    const auto vpn1 = (virtual_2m >> 21U) & 0x1FFU;
    page_2m_bus.store_doubleword(
        root_address + vpn2 * 8U,
        make_pte(
            level_one_address,
            rv64::sv39_pte_bits::valid));
    const rv::PhysAddr leaf_2m =
        level_one_address + vpn1 * 8U;
    page_2m_bus.store_doubleword(
        leaf_2m,
        make_pte(
            physical_2m,
            rx | rv64::sv39_pte_bits::valid));
    result = rv64::translate_address(
        page_2m_bus,
        page_2m_state,
        virtual_2m,
        rv64::MemoryAccessType::Load);
    CHECK(result.ready());
    CHECK(
        result.physical_address ==
        physical_2m + (virtual_2m & 0x1FFFFFU));

    MemoryBus page_1g_bus;
    rv64::CpuSnapshot page_1g_state;
    page_1g_state.privilege = rv64::PrivilegeMode::Supervisor;
    enable_sv39(page_1g_state);
    constexpr std::uint64_t virtual_1g = 0x0000000147654321ULL;
    constexpr rv::PhysAddr physical_1g = 0x0000000840000000ULL;
    const rv::PhysAddr leaf_1g =
        root_address + ((virtual_1g >> 30U) & 0x1FFU) * 8U;
    page_1g_bus.store_doubleword(
        leaf_1g,
        make_pte(
            physical_1g,
            rx | rv64::sv39_pte_bits::valid));
    result = rv64::translate_address(
        page_1g_bus,
        page_1g_state,
        virtual_1g,
        rv64::MemoryAccessType::Load);
    CHECK(result.ready());
    CHECK(
        result.physical_address ==
        physical_1g + (virtual_1g & 0x3FFFFFFFU));

    page_1g_bus.store_doubleword(
        leaf_1g,
        make_pte(
            physical_1g + 0x200000U,
            rx | rv64::sv39_pte_bits::valid));
    CHECK(
        rv64::translate_address(
            page_1g_bus,
            page_1g_state,
            virtual_1g,
            rv64::MemoryAccessType::Load)
            .status == rv64::TranslationStatus::PageFault);
}

void test_invalid_and_reserved_entries()
{
    constexpr std::uint64_t virtual_address = 0x0000000040403000ULL;
    constexpr rv::PhysAddr physical_address = 0x9000U;
    constexpr std::uint64_t rw =
        rv64::sv39_pte_bits::read |
        rv64::sv39_pte_bits::write |
        rv64::sv39_pte_bits::accessed |
        rv64::sv39_pte_bits::dirty;

    MemoryBus bus;
    rv64::CpuSnapshot state;
    state.privilege = rv64::PrivilegeMode::Supervisor;
    const Mapping mapping = map_4k(
        bus,
        state,
        virtual_address,
        physical_address,
        rw);

    bus.store_doubleword(mapping.leaf_pte, 0U);
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .status == rv64::TranslationStatus::PageFault);

    bus.store_doubleword(
        mapping.leaf_pte,
        rv64::sv39_pte_bits::valid |
            rv64::sv39_pte_bits::write);
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Store)
            .status == rv64::TranslationStatus::PageFault);

    bus.store_doubleword(
        mapping.leaf_pte,
        make_pte(
            physical_address,
            rw |
                rv64::sv39_pte_bits::valid |
                (std::uint64_t{1} << 63U)));
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .status == rv64::TranslationStatus::PageFault);

    bus.store_doubleword(
        mapping.level_one_pte,
        make_pte(
            level_zero_address,
            rv64::sv39_pte_bits::valid |
                rv64::sv39_pte_bits::user));
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .status == rv64::TranslationStatus::PageFault);
}

void test_permissions_sum_mxr_and_mprv()
{
    constexpr std::uint64_t virtual_address = 0x0000000050001000ULL;
    constexpr rv::PhysAddr physical_address = 0xA000U;
    constexpr std::uint64_t user_rwx =
        rv64::sv39_pte_bits::user |
        rv64::sv39_pte_bits::read |
        rv64::sv39_pte_bits::write |
        rv64::sv39_pte_bits::execute |
        rv64::sv39_pte_bits::accessed |
        rv64::sv39_pte_bits::dirty;

    MemoryBus bus;
    rv64::CpuSnapshot state;
    state.privilege = rv64::PrivilegeMode::Supervisor;
    const Mapping mapping = map_4k(
        bus,
        state,
        virtual_address,
        physical_address,
        user_rwx);

    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .status == rv64::TranslationStatus::PageFault);
    state.machine_csrs.mstatus = rv64::mstatus_bits::sum;
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .ready());
    CHECK(
        rv64::translate_address(
            bus,
            state,
            virtual_address,
            rv64::MemoryAccessType::InstructionFetch)
            .status == rv64::TranslationStatus::PageFault);

    state.privilege = rv64::PrivilegeMode::User;
    state.machine_csrs.mstatus = 0;
    CHECK(
        rv64::translate_address(
            bus,
            state,
            virtual_address,
            rv64::MemoryAccessType::InstructionFetch)
            .ready());
    bus.store_doubleword(
        mapping.leaf_pte,
        make_pte(
            physical_address,
            rv64::sv39_pte_bits::valid |
                rv64::sv39_pte_bits::execute |
                rv64::sv39_pte_bits::user |
                rv64::sv39_pte_bits::accessed));
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .status == rv64::TranslationStatus::PageFault);
    state.machine_csrs.mstatus = rv64::mstatus_bits::mxr;
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .ready());

    state.privilege = rv64::PrivilegeMode::Machine;
    state.machine_csrs.mstatus =
        rv64::mstatus_bits::mprv |
        (static_cast<std::uint64_t>(
             rv64::PrivilegeMode::User)
         << rv64::mstatus_bits::mpp_shift) |
        rv64::mstatus_bits::mxr;
    CHECK(
        rv64::effective_privilege(
            state,
            rv64::MemoryAccessType::Load) ==
        rv64::PrivilegeMode::User);
    CHECK(
        rv64::translate_address(
            bus, state, virtual_address, rv64::MemoryAccessType::Load)
            .ready());
    const auto fetch = rv64::translate_address(
        bus,
        state,
        0x0000004000000000ULL,
        rv64::MemoryAccessType::InstructionFetch);
    CHECK(fetch.ready());
    CHECK(fetch.physical_address == 0x0000004000000000ULL);
}

void test_accessed_dirty_and_walk_access_faults()
{
    constexpr std::uint64_t virtual_address = 0x0000000060002120ULL;
    constexpr rv::PhysAddr physical_address = 0xB000U;
    constexpr std::uint64_t rw =
        rv64::sv39_pte_bits::read |
        rv64::sv39_pte_bits::write;

    MemoryBus bus;
    rv64::CpuSnapshot state;
    state.privilege = rv64::PrivilegeMode::Supervisor;
    const Mapping mapping = map_4k(
        bus,
        state,
        virtual_address,
        physical_address,
        rw);
    auto result = rv64::translate_address(
        bus,
        state,
        virtual_address,
        rv64::MemoryAccessType::Load);
    CHECK(result.ready());
    CHECK(bus.page_walk_writes == 1U);
    CHECK(
        (bus.load_doubleword(mapping.leaf_pte) &
         rv64::sv39_pte_bits::accessed) != 0U);
    CHECK(
        (bus.load_doubleword(mapping.leaf_pte) &
         rv64::sv39_pte_bits::dirty) == 0U);

    result = rv64::translate_address(
        bus,
        state,
        virtual_address,
        rv64::MemoryAccessType::Store);
    CHECK(result.ready());
    CHECK(bus.page_walk_writes == 2U);
    CHECK(
        (bus.load_doubleword(mapping.leaf_pte) &
         rv64::sv39_pte_bits::dirty) != 0U);

    bus.read_fault_address = mapping.root_pte;
    result = rv64::translate_address(
        bus,
        state,
        virtual_address,
        rv64::MemoryAccessType::Load);
    CHECK(result.status == rv64::TranslationStatus::AccessFault);
    CHECK(result.bus_fault == rv::BusFault::Unmapped);
    bus.read_fault_address.reset();

    bus.store_doubleword(
        mapping.leaf_pte,
        make_pte(physical_address, rw | rv64::sv39_pte_bits::valid));
    bus.write_fault_address = mapping.leaf_pte;
    result = rv64::translate_address(
        bus,
        state,
        virtual_address,
        rv64::MemoryAccessType::Store);
    CHECK(result.status == rv64::TranslationStatus::AccessFault);
    CHECK(result.bus_fault == rv::BusFault::ReadOnly);
}

void test_tlb_asids_global_and_fence_scopes()
{
    constexpr std::uint64_t virtual_address = 0x0000000070003124ULL;
    constexpr rv::PhysAddr physical_a = 0xC000U;
    constexpr rv::PhysAddr physical_b = 0xD000U;
    constexpr std::uint64_t rw =
        rv64::sv39_pte_bits::read |
        rv64::sv39_pte_bits::write |
        rv64::sv39_pte_bits::accessed |
        rv64::sv39_pte_bits::dirty;

    MemoryBus bus;
    rv64::CpuSnapshot state;
    state.privilege = rv64::PrivilegeMode::Supervisor;
    const Mapping mapping = map_4k(
        bus,
        state,
        virtual_address,
        physical_a,
        rw);
    enable_sv39(state, 1U);
    rv64::Sv39Tlb tlb;
    rv64::MmuPerformanceCounters counters;
    auto result = tlb.translate(
        bus,
        state,
        virtual_address,
        rv64::MemoryAccessType::Load,
        &counters);
    CHECK(result.physical_address == physical_a + 0x124U);
    CHECK(counters.tlb_misses == 1U);
    CHECK(tlb.valid_entries() == 1U);

    result = tlb.translate(
        bus,
        state,
        virtual_address,
        rv64::MemoryAccessType::Load,
        &counters);
    CHECK(result.physical_address == physical_a + 0x124U);
    CHECK(counters.tlb_hits == 1U);

    bus.store_doubleword(
        mapping.leaf_pte,
        make_pte(
            physical_b,
            rw | rv64::sv39_pte_bits::valid));
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv64::MemoryAccessType::Load)
            .physical_address ==
        physical_a + 0x124U);
    tlb.sfence_vma(virtual_address, std::nullopt);
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv64::MemoryAccessType::Load)
            .physical_address ==
        physical_b + 0x124U);

    enable_sv39(state, 2U);
    bus.store_doubleword(
        mapping.leaf_pte,
        make_pte(
            physical_a,
            rw | rv64::sv39_pte_bits::valid));
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv64::MemoryAccessType::Load)
            .physical_address ==
        physical_a + 0x124U);
    enable_sv39(state, 1U);
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv64::MemoryAccessType::Load)
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
               rv64::MemoryAccessType::Load)
            .physical_address ==
        physical_a + 0x124U);

    bus.store_doubleword(
        mapping.leaf_pte,
        make_pte(
            physical_b,
            rw |
                rv64::sv39_pte_bits::global |
                rv64::sv39_pte_bits::valid));
    tlb.sfence_vma(std::nullopt, std::nullopt);
    CHECK(
        tlb.translate(
               bus,
               state,
               virtual_address,
               rv64::MemoryAccessType::Load)
            .physical_address ==
        physical_b + 0x124U);
    tlb.sfence_vma(
        std::nullopt,
        std::optional<std::uint16_t>{1U});
    CHECK(tlb.valid_entries() == 1U);
    tlb.sfence_vma(std::nullopt, std::nullopt);
    CHECK(tlb.valid_entries() == 0U);
}

void test_core_fetch_load_and_page_fault_integration()
{
    constexpr rv::PhysAddr boot_pc = 0x80000000U;
    constexpr std::uint64_t virtual_code = 0x0000000040000000ULL;
    constexpr std::uint64_t virtual_data = 0x0000000040001000ULL;
    constexpr rv::PhysAddr physical_code = 0x4000U;
    constexpr rv::PhysAddr physical_data = 0x5000U;
    constexpr std::uint64_t code_flags =
        rv64::sv39_pte_bits::read |
        rv64::sv39_pte_bits::execute |
        rv64::sv39_pte_bits::accessed;
    constexpr std::uint64_t data_flags =
        rv64::sv39_pte_bits::read |
        rv64::sv39_pte_bits::write |
        rv64::sv39_pte_bits::accessed |
        rv64::sv39_pte_bits::dirty;

    MemoryBus bus;
    rv64::CpuSnapshot page_state;
    page_state.privilege = rv64::PrivilegeMode::Supervisor;
    map_4k(
        bus,
        page_state,
        virtual_code,
        physical_code,
        code_flags);
    const Mapping data_mapping = map_4k(
        bus,
        page_state,
        virtual_data,
        physical_data,
        data_flags);

    const std::uint32_t boot_program[]{
        encode_i(1U, 0U, 0U, 1U, 0x13U),
        encode_i(63U, 1U, 1U, 1U, 0x13U),
        encode_i(1U, 1U, 0U, 1U, 0x13U),
        encode_csr(rv64::csr_address::satp, 1U),
        encode_u(static_cast<std::uint32_t>(virtual_code), 2U),
        encode_csr(rv64::csr_address::mepc, 2U),
        encode_i(1U, 0U, 0U, 3U, 0x13U),
        encode_i(11U, 3U, 1U, 3U, 0x13U),
        encode_csr(rv64::csr_address::mstatus, 3U),
        0x30200073U,
    };
    for (std::size_t index = 0;
         index < std::size(boot_program);
         ++index) {
        bus.store_word(
            boot_pc + index * 4U,
            boot_program[index]);
    }
    bus.store_word(
        physical_code,
        encode_u(static_cast<std::uint32_t>(virtual_data), 4U));
    bus.store_word(
        physical_code + 4U,
        encode_i(0U, 4U, 3U, 5U, 0x03U));
    bus.store_word(
        physical_code + 8U,
        encode_i(42U, 0U, 0U, 7U, 0x13U));
    bus.store_word(
        physical_code + 12U,
        (2U << 27U) |
            (4U << 15U) |
            (3U << 12U) |
            (5U << 7U) |
            0x2FU);
    bus.store_word(
        physical_code + 16U,
        (3U << 27U) |
            (7U << 20U) |
            (4U << 15U) |
            (3U << 12U) |
            (6U << 7U) |
            0x2FU);
    bus.store_word(physical_code + 20U, 0x12000073U);
    bus.store_word(
        physical_code + 24U,
        encode_i(0U, 4U, 3U, 6U, 0x03U));
    bus.store_doubleword(
        physical_data,
        0x1122334455667788ULL);

    rv64::Core core(bus);
    core.reset({.reset_pc = boot_pc});
    for (std::size_t index = 0;
         index < std::size(boot_program);
         ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    CHECK(core.snapshot().privilege == rv64::PrivilegeMode::Supervisor);
    CHECK(core.snapshot().pc == virtual_code);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(
        core.snapshot().registers[5] ==
        0x1122334455667788ULL);
    CHECK(core.tlb_entries() >= 2U);

    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.snapshot().registers[6] == 0U);
    CHECK(bus.load_doubleword(physical_data) == 42U);

    bus.store_doubleword(data_mapping.leaf_pte, 0U);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.tlb_entries() == 0U);
    const auto fault = core.step();
    CHECK(fault.status == rv64::StepStatus::TrapTaken);
    CHECK(fault.trap_value == virtual_data);
    CHECK(fault.bus_fault == rv::BusFault::None);
    CHECK(
        core.snapshot().machine_csrs.mcause ==
        static_cast<std::uint64_t>(
            rv64::ExceptionCause::LoadPageFault));
    CHECK(core.snapshot().machine_csrs.mtval == virtual_data);
}

} // namespace

int main()
{
    test_satp_bare_machine_and_canonical_addresses();
    test_base_and_superpage_translation();
    test_invalid_and_reserved_entries();
    test_permissions_sum_mxr_and_mprv();
    test_accessed_dirty_and_walk_access_faults();
    test_tlb_asids_global_and_fence_scopes();
    test_core_fetch_load_and_page_fault_integration();

    if (failures == 0) {
        std::cout << "All independent RV64 Sv39 tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
