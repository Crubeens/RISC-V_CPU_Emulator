#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

#include "rv32/core/execute.hpp"

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

class RecordingDataBus final : public rv32::CpuBus {
  public:
    rv32::ReadResult next_read{};
    rv32::BusFault next_write{rv32::BusFault::None};

    std::uint32_t read_count{};
    std::uint32_t write_count{};
    rv32::PhysAddr last_address{};
    rv32::AccessWidth last_width{rv32::AccessWidth::Byte};
    rv32::AccessKind last_kind{rv32::AccessKind::InstructionFetch};
    std::uint64_t last_value{};

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
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        std::uint64_t value,
        rv32::AccessKind kind) override
    {
        ++write_count;
        last_address = address;
        last_width = width;
        last_kind = kind;
        last_value = value;
        return next_write;
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

struct LoadCase {
    std::string_view name;
    rv32::InstructionKind kind;
    rv32::AccessWidth width;
    std::uint64_t bus_value;
    std::uint32_t expected;
};

constexpr std::array load_cases{
    LoadCase{
        "lb positive",
        rv32::InstructionKind::Lb,
        rv32::AccessWidth::Byte,
        0x1234007FU,
        0x0000007FU,
    },
    LoadCase{
        "lb sign extends",
        rv32::InstructionKind::Lb,
        rv32::AccessWidth::Byte,
        0x1234FF80U,
        0xFFFFFF80U,
    },
    LoadCase{
        "lbu positive",
        rv32::InstructionKind::Lbu,
        rv32::AccessWidth::Byte,
        0x1234007FU,
        0x0000007FU,
    },
    LoadCase{
        "lbu zero extends",
        rv32::InstructionKind::Lbu,
        rv32::AccessWidth::Byte,
        0x1234FFFFU,
        0x000000FFU,
    },
    LoadCase{
        "lh positive",
        rv32::InstructionKind::Lh,
        rv32::AccessWidth::HalfWord,
        0x12347FFFU,
        0x00007FFFU,
    },
    LoadCase{
        "lh sign extends",
        rv32::InstructionKind::Lh,
        rv32::AccessWidth::HalfWord,
        0x12348001U,
        0xFFFF8001U,
    },
    LoadCase{
        "lhu positive",
        rv32::InstructionKind::Lhu,
        rv32::AccessWidth::HalfWord,
        0x12347FFFU,
        0x00007FFFU,
    },
    LoadCase{
        "lhu zero extends",
        rv32::InstructionKind::Lhu,
        rv32::AccessWidth::HalfWord,
        0x1234FFFFU,
        0x0000FFFFU,
    },
    LoadCase{
        "lw normal",
        rv32::InstructionKind::Lw,
        rv32::AccessWidth::Word,
        0x12345678U,
        0x12345678U,
    },
    LoadCase{
        "lw keeps low word",
        rv32::InstructionKind::Lw,
        rv32::AccessWidth::Word,
        0x12345678FFFFFFFFULL,
        0xFFFFFFFFU,
    },
};

void test_successful_loads()
{
    constexpr std::uint32_t pc = 0x80000000U;
    constexpr std::uint32_t address_base = 0x1000U;

    for (const auto& test : load_cases) {
        RecordingDataBus bus;
        bus.next_read = {
            .fault = rv32::BusFault::None,
            .value = test.bus_value,
        };
        const rv32::DecodedInstruction decoded{
            .kind = test.kind,
            .raw = 0xA5A50003U,
            .rd = 5U,
            .rs1 = 1U,
            .rs2 = 0,
            .immediate = 4U,
        };

        const auto result = rv32::execute_memory(
            bus,
            decoded,
            pc,
            address_base,
            0);

        if (!result.ready() ||
            !result.pending.ready() ||
            !result.pending.register_write.enabled ||
            result.pending.register_write.index != 5U ||
            result.pending.register_write.value != test.expected) {
            std::cerr << "FAIL load case \"" << test.name << "\"\n";
            ++failures;
        }
        CHECK(result.pending.pc == pc);
        CHECK(result.pending.instruction == decoded.raw);
        CHECK(result.pending.next_pc == pc + 4U);
        CHECK(result.bus_fault == rv32::BusFault::None);
        CHECK(result.trap_value == 0U);
        CHECK(bus.read_count == 1U);
        CHECK(bus.write_count == 0U);
        CHECK(bus.last_address == address_base + 4U);
        CHECK(bus.last_width == test.width);
        CHECK(bus.last_kind == rv32::AccessKind::Load);
    }
}

void test_successful_stores()
{
    struct StoreCase {
        std::string_view name;
        rv32::InstructionKind kind;
        rv32::AccessWidth width;
        std::uint32_t rs1_value;
        std::uint32_t immediate;
        std::uint32_t value;
        std::uint32_t expected_address;
    };
    constexpr std::array cases{
        StoreCase{
            "sb normal",
            rv32::InstructionKind::Sb,
            rv32::AccessWidth::Byte,
            0x1000U,
            8U,
            0xAABBCCDDU,
            0x1008U,
        },
        StoreCase{
            "sb address wraps",
            rv32::InstructionKind::Sb,
            rv32::AccessWidth::Byte,
            0xFFFFFFFFU,
            1U,
            0xFFFFFFFFU,
            0U,
        },
        StoreCase{
            "sh normal",
            rv32::InstructionKind::Sh,
            rv32::AccessWidth::HalfWord,
            0x1000U,
            8U,
            0xAABBCCDDU,
            0x1008U,
        },
        StoreCase{
            "sh address wraps",
            rv32::InstructionKind::Sh,
            rv32::AccessWidth::HalfWord,
            0xFFFFFFFEU,
            2U,
            0xFFFFFFFFU,
            0U,
        },
        StoreCase{
            "sw normal",
            rv32::InstructionKind::Sw,
            rv32::AccessWidth::Word,
            0x1000U,
            8U,
            0xAABBCCDDU,
            0x1008U,
        },
        StoreCase{
            "sw address wraps",
            rv32::InstructionKind::Sw,
            rv32::AccessWidth::Word,
            0xFFFFFFFCU,
            4U,
            0xFFFFFFFFU,
            0U,
        },
    };

    for (const auto& test : cases) {
        RecordingDataBus bus;
        const rv32::DecodedInstruction decoded{
            .kind = test.kind,
            .raw = 0xA5A50023U,
            .rd = 0,
            .rs1 = 1U,
            .rs2 = 2U,
            .immediate = test.immediate,
        };

        const auto result = rv32::execute_memory(
            bus,
            decoded,
            0x80000000U,
            test.rs1_value,
            test.value);

        if (!result.ready()) {
            std::cerr << "FAIL store case \"" << test.name << "\"\n";
            ++failures;
            continue;
        }
        CHECK(result.pending.ready());
        CHECK(result.pending.next_pc == 0x80000004U);
        CHECK(!result.pending.register_write.enabled);
        CHECK(result.bus_fault == rv32::BusFault::None);
        CHECK(result.trap_value == 0U);
        CHECK(bus.read_count == 0U);
        CHECK(bus.write_count == 1U);
        CHECK(bus.last_address == test.expected_address);
        CHECK(bus.last_width == test.width);
        CHECK(bus.last_kind == rv32::AccessKind::Store);
        CHECK(bus.last_value == test.value);
    }
}

void test_effective_address_wraps_to_32_bits()
{
    RecordingDataBus bus;
    const rv32::DecodedInstruction decoded{
        .kind = rv32::InstructionKind::Lbu,
        .raw = 0,
        .rd = 1U,
        .rs1 = 2U,
        .rs2 = 0,
        .immediate = 4U,
    };

    const auto result = rv32::execute_memory(
        bus,
        decoded,
        0x80000000U,
        0xFFFFFFFCU,
        0);

    CHECK(result.ready());
    CHECK(bus.last_address == 0U);
}

void test_misalignment_is_rejected_before_bus_access()
{
    struct MisalignedCase {
        rv32::InstructionKind kind;
        std::uint32_t address;
        rv32::MemoryStatus status;
    };
    constexpr std::array cases{
        MisalignedCase{
            rv32::InstructionKind::Lh,
            0x1001U,
            rv32::MemoryStatus::LoadAddressMisaligned,
        },
        MisalignedCase{
            rv32::InstructionKind::Lw,
            0x1002U,
            rv32::MemoryStatus::LoadAddressMisaligned,
        },
        MisalignedCase{
            rv32::InstructionKind::Sh,
            0x1001U,
            rv32::MemoryStatus::StoreAddressMisaligned,
        },
        MisalignedCase{
            rv32::InstructionKind::Sw,
            0x1002U,
            rv32::MemoryStatus::StoreAddressMisaligned,
        },
    };

    for (const auto& test : cases) {
        RecordingDataBus bus;
        const rv32::DecodedInstruction decoded{
            .kind = test.kind,
            .raw = 0,
            .rd = 1U,
            .rs1 = 2U,
            .rs2 = 3U,
            .immediate = 0,
        };

        const auto result = rv32::execute_memory(
            bus,
            decoded,
            0x80000000U,
            test.address,
            0x12345678U);

        CHECK(result.status == test.status);
        CHECK(!result.pending.ready());
        CHECK(result.bus_fault == rv32::BusFault::Misaligned);
        CHECK(result.trap_value == test.address);
        CHECK(bus.read_count == 0U);
        CHECK(bus.write_count == 0U);
    }
}

void test_bus_faults_are_preserved()
{
    constexpr std::array access_faults{
        rv32::BusFault::Unmapped,
        rv32::BusFault::OutOfRange,
        rv32::BusFault::ReadOnly,
        rv32::BusFault::Unsupported,
        rv32::BusFault::DeviceError,
    };

    for (const auto fault : access_faults) {
        RecordingDataBus bus;
        bus.next_read = {.fault = fault};
        const rv32::DecodedInstruction decoded{
            .kind = rv32::InstructionKind::Lw,
            .raw = 0,
            .rd = 1U,
            .rs1 = 2U,
            .rs2 = 0,
            .immediate = 0,
        };
        const auto result = rv32::execute_memory(
            bus,
            decoded,
            0x80000000U,
            0x1000U,
            0);

        CHECK(result.status == rv32::MemoryStatus::LoadAccessFault);
        CHECK(!result.pending.ready());
        CHECK(result.bus_fault == fault);
        CHECK(result.trap_value == 0x1000U);
        CHECK(bus.read_count == 1U);
    }

    {
        RecordingDataBus bus;
        bus.next_read = {.fault = rv32::BusFault::Misaligned};
        const rv32::DecodedInstruction decoded{
            .kind = rv32::InstructionKind::Lw,
            .raw = 0,
            .rd = 1U,
            .rs1 = 2U,
            .rs2 = 0,
            .immediate = 0,
        };
        const auto result = rv32::execute_memory(
            bus,
            decoded,
            0x80000000U,
            0x1000U,
            0);

        CHECK(
            result.status ==
            rv32::MemoryStatus::LoadAddressMisaligned);
        CHECK(result.bus_fault == rv32::BusFault::Misaligned);
        CHECK(result.trap_value == 0x1000U);
        CHECK(bus.read_count == 1U);
    }

    for (const auto fault : access_faults) {
        RecordingDataBus bus;
        bus.next_write = fault;
        const rv32::DecodedInstruction decoded{
            .kind = rv32::InstructionKind::Sw,
            .raw = 0,
            .rd = 0,
            .rs1 = 2U,
            .rs2 = 3U,
            .immediate = 0,
        };
        const auto result = rv32::execute_memory(
            bus,
            decoded,
            0x80000000U,
            0x1000U,
            0x12345678U);

        CHECK(result.status == rv32::MemoryStatus::StoreAccessFault);
        CHECK(!result.pending.ready());
        CHECK(result.bus_fault == fault);
        CHECK(result.trap_value == 0x1000U);
        CHECK(bus.write_count == 1U);
    }

    {
        RecordingDataBus bus;
        bus.next_write = rv32::BusFault::Misaligned;
        const rv32::DecodedInstruction decoded{
            .kind = rv32::InstructionKind::Sw,
            .raw = 0,
            .rd = 0,
            .rs1 = 2U,
            .rs2 = 3U,
            .immediate = 0,
        };
        const auto result = rv32::execute_memory(
            bus,
            decoded,
            0x80000000U,
            0x1000U,
            0x12345678U);

        CHECK(
            result.status ==
            rv32::MemoryStatus::StoreAddressMisaligned);
        CHECK(result.bus_fault == rv32::BusFault::Misaligned);
        CHECK(result.trap_value == 0x1000U);
        CHECK(bus.write_count == 1U);
    }
}

void test_load_to_x0_still_reads_bus()
{
    RecordingDataBus bus;
    bus.next_read = {
        .fault = rv32::BusFault::None,
        .value = 0x12345678U,
    };
    const rv32::DecodedInstruction decoded{
        .kind = rv32::InstructionKind::Lw,
        .raw = 0,
        .rd = 0,
        .rs1 = 1U,
        .rs2 = 0,
        .immediate = 0,
    };

    const auto result = rv32::execute_memory(
        bus,
        decoded,
        0x80000000U,
        0x1000U,
        0);

    CHECK(result.ready());
    CHECK(result.pending.register_write.enabled);
    CHECK(result.pending.register_write.index == 0U);
    CHECK(bus.read_count == 1U);
}

void test_non_memory_instruction_has_no_side_effect()
{
    RecordingDataBus bus;
    const rv32::DecodedInstruction decoded{
        .kind = rv32::InstructionKind::Addi,
        .raw = 0x00100093U,
        .rd = 1U,
        .rs1 = 0,
        .rs2 = 0,
        .immediate = 1U,
    };

    const auto result =
        rv32::execute_memory(bus, decoded, 0x80000000U, 0, 0);

    CHECK(
        result.status ==
        rv32::MemoryStatus::NotMemoryInstruction);
    CHECK(!result.pending.ready());
    CHECK(bus.read_count == 0U);
    CHECK(bus.write_count == 0U);
}

} // namespace

int main()
{
    test_successful_loads();
    test_successful_stores();
    test_effective_address_wraps_to_32_bits();
    test_misalignment_is_rejected_before_bus_access();
    test_bus_faults_are_preserved();
    test_load_to_x0_still_reads_bus();
    test_non_memory_instruction_has_no_side_effect();

    if (failures == 0) {
        std::cout << "All RV32 memory execution tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
