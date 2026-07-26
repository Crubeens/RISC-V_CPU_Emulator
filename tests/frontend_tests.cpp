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
        return next_read;
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
    const auto result = rv32::fetch_decode(bus, 0x80000002U);

    CHECK(
        result.status ==
        rv32::FrontendStatus::InstructionAddressMisaligned);
    CHECK(result.pc == 0x80000002U);
    CHECK(result.instruction == 0U);
    CHECK(!result.decoded.valid());
    CHECK(result.bus_fault == rv32::BusFault::Misaligned);
    CHECK(result.trap_value == 0x80000002U);
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

    CHECK(bus.read_count == 1U);
    CHECK(bus.write_count == 0U);
    CHECK(bus.last_address == 0x80000000ULL);
    CHECK(bus.last_width == rv32::AccessWidth::Word);
    CHECK(bus.last_kind == rv32::AccessKind::InstructionFetch);
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
    CHECK(bus.read_count == 1U);
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

} // namespace

int main()
{
    test_misaligned_pc_does_not_access_bus();
    test_valid_instruction_fetch_and_decode();
    test_illegal_instruction_preserves_raw_value();
    test_bus_faults_are_preserved();
    test_bus_misalignment_is_not_lost();

    if (failures == 0) {
        std::cout << "All RV32 frontend tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
