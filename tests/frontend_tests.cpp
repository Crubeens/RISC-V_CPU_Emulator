#include <array>
#include <cstdint>
#include <iostream>

#include "rv32/core/frontend.hpp"

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

class RecordingBus final : public rv32::CpuBus {
  public:
    rv32::ReadResult next_read{};
    std::array<rv32::ReadResult, 2> scripted_reads{};
    std::array<rv32::PhysAddr, 2> read_addresses{};
    bool use_scripted_reads{};

    std::uint32_t read_count{};
    std::uint32_t write_count{};
    rv32::PhysAddr last_address{};
    rv32::AccessWidth last_width{rv32::AccessWidth::Byte};
    rv32::AccessKind last_kind{rv32::AccessKind::Load};

    rv32::ReadResult read(
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        rv32::AccessKind kind) override
    {
        ++read_count;
        last_address = address;
        last_width = width;
        last_kind = kind;
        if (read_count <= read_addresses.size()) {
            read_addresses[read_count - 1U] = address;
        }
        if (use_scripted_reads) {
            if (read_count > scripted_reads.size()) {
                return {.fault = rv32::BusFault::DeviceError};
            }
            return scripted_reads[read_count - 1U];
        }
        if (!next_read.ok() ||
            width != rv32::AccessWidth::HalfWord) {
            return next_read;
        }
        return {
            .fault = rv32::BusFault::None,
            .value =
                (next_read.value >>
                 (static_cast<unsigned int>(address & 0x2U) * 8U)) &
                0xFFFFU,
        };
    }

    rv32::BusFault write(
        rv32::PhysAddr,
        rv32::AccessWidth,
        std::uint64_t,
        rv32::AccessKind) override
    {
        ++write_count;
        return rv32::BusFault::DeviceError;
    }

    rv32::ReadResult load_reserved_word(
        std::uint32_t,
        rv32::PhysAddr) override
    {
        return {.fault = rv32::BusFault::DeviceError};
    }

    rv32::StoreConditionalResult store_conditional_word(
        std::uint32_t,
        rv32::PhysAddr,
        std::uint32_t) override
    {
        return {
            .fault = rv32::BusFault::DeviceError,
            .succeeded = false,
        };
    }

    rv32::AtomicResult atomic_word(
        std::uint32_t,
        rv32::PhysAddr,
        rv32::AmoOperation,
        std::uint32_t) override
    {
        return {.fault = rv32::BusFault::DeviceError};
    }

    std::uint64_t read_time() const noexcept override
    {
        return 0;
    }
};

void test_misaligned_pc_does_not_access_bus()
{
    RecordingBus bus;
    const auto result = rv32::fetch_decode(bus, 0x80000001U);

    CHECK(
        result.status ==
        rv32::FrontendStatus::InstructionAddressMisaligned);
    CHECK(result.pc == 0x80000001U);
    CHECK(result.instruction == 0U);
    CHECK(!result.decoded.valid());
    CHECK(result.bus_fault == rv32::BusFault::Misaligned);
    CHECK(result.trap_value == 0x80000001U);
    CHECK(bus.read_count == 0U);
    CHECK(bus.write_count == 0U);
}

void test_valid_instruction_fetch_and_decode()
{
    RecordingBus bus;
    bus.next_read = {
        .fault = rv32::BusFault::None,
        .value = 0x00500093U,
    };

    const auto result = rv32::fetch_decode(bus, 0x80000000U);

    CHECK(result.status == rv32::FrontendStatus::Ready);
    CHECK(result.ready());
    CHECK(result.pc == 0x80000000U);
    CHECK(result.instruction == 0x00500093U);
    CHECK(result.decoded.kind == rv32::InstructionKind::Addi);
    CHECK(result.decoded.raw == 0x00500093U);
    CHECK(result.decoded.rd == 1U);
    CHECK(result.decoded.rs1 == 0U);
    CHECK(result.decoded.rs2 == 0U);
    CHECK(result.decoded.immediate == 5U);
    CHECK(result.bus_fault == rv32::BusFault::None);
    CHECK(result.trap_value == 0U);

    CHECK(bus.read_count == 2U);
    CHECK(bus.write_count == 0U);
    CHECK(bus.last_address == 0x80000002ULL);
    CHECK(bus.last_width == rv32::AccessWidth::HalfWord);
    CHECK(bus.last_kind == rv32::AccessKind::InstructionFetch);
}

void test_compressed_instruction_at_halfword_boundary()
{
    RecordingBus bus;
    bus.use_scripted_reads = true;
    bus.scripted_reads[0] = {
        .fault = rv32::BusFault::None,
        .value = 0x4415U,
    };

    const auto result = rv32::fetch_decode(bus, 0x80000002U);

    CHECK(result.status == rv32::FrontendStatus::Ready);
    CHECK(result.ready());
    CHECK(result.pc == 0x80000002U);
    CHECK(result.instruction == 0x4415U);
    CHECK(result.decoded.kind == rv32::InstructionKind::Addi);
    CHECK(result.decoded.rd == 8U);
    CHECK(result.decoded.rs1 == 0U);
    CHECK(result.decoded.immediate == 5U);
    CHECK(result.decoded.length == 2U);
    CHECK(result.trap_value == 0U);
    CHECK(bus.read_count == 1U);
    CHECK(bus.read_addresses[0] == 0x80000002ULL);
}

void test_standard_instruction_at_halfword_boundary()
{
    RecordingBus bus;
    bus.use_scripted_reads = true;
    bus.scripted_reads = {{
        {
            .fault = rv32::BusFault::None,
            .value = 0x0093U,
        },
        {
            .fault = rv32::BusFault::None,
            .value = 0x0050U,
        },
    }};

    const auto result = rv32::fetch_decode(bus, 0x80000002U);

    CHECK(result.status == rv32::FrontendStatus::Ready);
    CHECK(result.instruction == 0x00500093U);
    CHECK(result.decoded.kind == rv32::InstructionKind::Addi);
    CHECK(result.decoded.rd == 1U);
    CHECK(result.decoded.immediate == 5U);
    CHECK(result.decoded.length == 4U);
    CHECK(bus.read_count == 2U);
    CHECK(bus.read_addresses[0] == 0x80000002ULL);
    CHECK(bus.read_addresses[1] == 0x80000004ULL);
}

void test_second_halfword_fault_reports_its_address()
{
    RecordingBus bus;
    bus.use_scripted_reads = true;
    bus.scripted_reads = {{
        {
            .fault = rv32::BusFault::None,
            .value = 0x0093U,
        },
        {
            .fault = rv32::BusFault::Unmapped,
            .value = 0,
        },
    }};

    const auto result = rv32::fetch_decode(bus, 0x80000FFEU);

    CHECK(
        result.status ==
        rv32::FrontendStatus::InstructionAccessFault);
    CHECK(!result.ready());
    CHECK(result.pc == 0x80000FFEU);
    CHECK(result.instruction == 0x0093U);
    CHECK(!result.decoded.valid());
    CHECK(result.bus_fault == rv32::BusFault::Unmapped);
    CHECK(result.trap_value == 0x80001000U);
    CHECK(bus.read_count == 2U);
    CHECK(bus.read_addresses[0] == 0x80000FFEULL);
    CHECK(bus.read_addresses[1] == 0x80001000ULL);
}

void test_illegal_compressed_instruction_preserves_raw_value()
{
    RecordingBus bus;
    bus.use_scripted_reads = true;
    bus.scripted_reads[0] = {
        .fault = rv32::BusFault::None,
        .value = 0x0000U,
    };

    const auto result = rv32::fetch_decode(bus, 0x80000000U);

    CHECK(
        result.status ==
        rv32::FrontendStatus::IllegalInstruction);
    CHECK(!result.ready());
    CHECK(result.instruction == 0U);
    CHECK(result.decoded.kind == rv32::InstructionKind::Illegal);
    CHECK(result.decoded.length == 2U);
    CHECK(result.bus_fault == rv32::BusFault::None);
    CHECK(result.trap_value == 0U);
    CHECK(bus.read_count == 1U);
}

void test_illegal_instruction_preserves_raw_value()
{
    RecordingBus bus;
    bus.next_read = {
        .fault = rv32::BusFault::None,
        .value = 0xFFFFFFFFU,
    };

    const auto result = rv32::fetch_decode(bus, 0x80000004U);

    CHECK(
        result.status ==
        rv32::FrontendStatus::IllegalInstruction);
    CHECK(!result.ready());
    CHECK(result.pc == 0x80000004U);
    CHECK(result.instruction == 0xFFFFFFFFU);
    CHECK(result.decoded.kind == rv32::InstructionKind::Illegal);
    CHECK(result.decoded.raw == 0xFFFFFFFFU);
    CHECK(result.bus_fault == rv32::BusFault::None);
    CHECK(result.trap_value == 0xFFFFFFFFU);
    CHECK(bus.read_count == 2U);
    CHECK(bus.write_count == 0U);
}

void test_bus_faults_are_preserved()
{
    constexpr std::array faults{
        rv32::BusFault::Unmapped,
        rv32::BusFault::OutOfRange,
        rv32::BusFault::ReadOnly,
        rv32::BusFault::Unsupported,
        rv32::BusFault::DeviceError,
    };

    for (const auto fault : faults) {
        RecordingBus bus;
        bus.next_read = {.fault = fault, .value = 0x00500093U};

        const auto result =
            rv32::fetch_decode(bus, 0x80000000U);

        CHECK(
            result.status ==
            rv32::FrontendStatus::InstructionAccessFault);
        CHECK(!result.ready());
        CHECK(result.pc == 0x80000000U);
        CHECK(result.instruction == 0U);
        CHECK(!result.decoded.valid());
        CHECK(result.bus_fault == fault);
        CHECK(result.trap_value == 0x80000000U);
        CHECK(bus.read_count == 1U);
        CHECK(bus.write_count == 0U);
    }
}

void test_bus_misalignment_is_not_lost()
{
    RecordingBus bus;
    bus.next_read = {
        .fault = rv32::BusFault::Misaligned,
        .value = 0,
    };

    const auto result = rv32::fetch_decode(bus, 0x80000000U);

    CHECK(
        result.status ==
        rv32::FrontendStatus::InstructionAddressMisaligned);
    CHECK(result.bus_fault == rv32::BusFault::Misaligned);
    CHECK(result.trap_value == 0x80000000U);
    CHECK(bus.read_count == 1U);
    CHECK(bus.write_count == 0U);
}

void test_decode_cache_hits_and_preserves_instruction_length()
{
    rv32::DecodeCache cache;
    rv32::DecodePerformanceCounters counters;

    const auto first = cache.decode(0x00500093U, 4U, &counters);
    const auto second = cache.decode(0x00500093U, 4U, &counters);
    CHECK(first.kind == rv32::InstructionKind::Addi);
    CHECK(second.kind == first.kind);
    CHECK(counters.lookups == 2U);
    CHECK(counters.misses == 1U);
    CHECK(counters.hits == 1U);
    CHECK(cache.valid_entries() == 1U);

    const auto compressed = cache.decode(0x00000001U, 2U, &counters);
    CHECK(compressed.length == 2U);
    CHECK(compressed.kind == rv32::InstructionKind::Addi);
    CHECK(cache.valid_entries() == 2U);

    cache.clear(&counters);
    CHECK(cache.valid_entries() == 0U);
    CHECK(counters.invalidations == 1U);
}

void test_instruction_cache_requires_explicit_invalidation()
{
    rv32::InstructionCache cache;
    rv32::InstructionCachePerformanceCounters counters;
    constexpr std::uint32_t virtual_address = 0x80001000U;
    const rv32::CachedInstruction cached{
        .instruction = 0x00108093U,
        .decoded = rv32::decode_instruction(0x00108093U),
    };

    CHECK(!cache.lookup(virtual_address, nullptr, &counters).has_value());
    cache.insert(virtual_address, nullptr, cached);
    const auto hit =
        cache.lookup(virtual_address, nullptr, &counters);
    CHECK(hit.has_value());
    CHECK(hit->instruction == cached.instruction);
    CHECK(hit->decoded.kind == rv32::InstructionKind::Addi);
    CHECK(counters.misses == 1U);
    CHECK(counters.hits == 1U);
    CHECK(cache.valid_entries() == 1U);

    cache.clear(&counters);
    CHECK(!cache.lookup(virtual_address, nullptr, &counters).has_value());
    CHECK(counters.invalidations == 1U);
    CHECK(cache.valid_entries() == 0U);
}

void test_instruction_cache_sfence_scope()
{
    rv32::InstructionCache cache;
    rv32::InstructionCachePerformanceCounters counters;
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    state.supervisor_csrs.satp =
        rv32::satp_bits::mode |
        (3U << 22U) |
        0x123U;
    const rv32::CachedInstruction cached{
        .instruction = 0x00108093U,
        .decoded = rv32::decode_instruction(0x00108093U),
    };
    constexpr std::uint32_t first_address = 0x40001000U;
    constexpr std::uint32_t second_address = 0x40008000U;

    cache.insert(first_address, &state, cached);
    cache.insert(second_address, &state, cached);
    cache.sfence_vma(first_address, 3U, &counters);

    CHECK(!cache.lookup(first_address, &state, &counters).has_value());
    CHECK(cache.lookup(second_address, &state, &counters).has_value());
    CHECK(cache.valid_entries() == 1U);
    CHECK(counters.invalidations == 1U);
}

} // namespace

int main()
{
    test_misaligned_pc_does_not_access_bus();
    test_valid_instruction_fetch_and_decode();
    test_compressed_instruction_at_halfword_boundary();
    test_standard_instruction_at_halfword_boundary();
    test_second_halfword_fault_reports_its_address();
    test_illegal_compressed_instruction_preserves_raw_value();
    test_illegal_instruction_preserves_raw_value();
    test_bus_faults_are_preserved();
    test_bus_misalignment_is_not_lost();
    test_decode_cache_hits_and_preserves_instruction_length();
    test_instruction_cache_requires_explicit_invalidation();
    test_instruction_cache_sfence_scope();

    if (failures == 0) {
        std::cout << "All RV32 frontend tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
