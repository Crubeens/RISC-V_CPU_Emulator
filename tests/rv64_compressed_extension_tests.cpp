#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "rv/common/bus.hpp"
#include "rv64/core/core.hpp"
#include "rv64/core/decode.hpp"
#include "rv64/core/trap.hpp"

namespace {

constexpr std::uint64_t base = 0x80000000ULL;
int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

struct DecodeCase {
    std::string_view name;
    std::uint16_t raw{};
    rv64::InstructionKind kind{rv64::InstructionKind::Illegal};
    std::uint8_t rd{};
    std::uint8_t rs1{};
    std::uint8_t rs2{};
    std::uint64_t immediate{};
};

void check_decode(const DecodeCase& expected)
{
    const auto actual =
        rv64::decode_compressed_instruction(expected.raw);
    if (actual.kind == expected.kind &&
        actual.raw == expected.raw &&
        actual.rd == expected.rd &&
        actual.rs1 == expected.rs1 &&
        actual.rs2 == expected.rs2 &&
        actual.immediate == expected.immediate &&
        actual.length == 2U &&
        actual.valid()) {
        return;
    }
    std::cerr
        << "FAIL compressed decode " << expected.name
        << " raw=0x" << std::hex << expected.raw
        << " kind=" << std::dec
        << static_cast<unsigned int>(actual.kind)
        << " rd=" << static_cast<unsigned int>(actual.rd)
        << " rs1=" << static_cast<unsigned int>(actual.rs1)
        << " rs2=" << static_cast<unsigned int>(actual.rs2)
        << " imm=0x" << std::hex << actual.immediate << std::dec << '\n';
    ++failures;
}

void check_illegal(std::uint16_t raw)
{
    const auto decoded = rv64::decode_compressed_instruction(raw);
    CHECK(!decoded.valid());
    CHECK(decoded.raw == raw);
    CHECK(decoded.length == 2U);
}

void test_rv64c_decode()
{
    using K = rv64::InstructionKind;
    constexpr DecodeCase cases[]{
        {"C.ADDI4SPN", 0x0800U, K::Addi, 8U, 2U, 0U, 16U},
        {"C.LW", 0x4004U, K::Lw, 9U, 8U, 0U, 0U},
        {"C.LD", 0x6004U, K::Ld, 9U, 8U, 0U, 0U},
        {"C.SW", 0xC444U, K::Sw, 0U, 8U, 9U, 12U},
        {"C.SD", 0xE404U, K::Sd, 0U, 8U, 9U, 8U},
        {"C.NOP", 0x0001U, K::Addi, 0U, 0U, 0U, 0U},
        {"C.ADDI", 0x147DU, K::Addi, 8U, 8U, 0U,
         0xFFFFFFFFFFFFFFFFULL},
        {"C.ADDIW", 0x2405U, K::Addiw, 8U, 8U, 0U, 1U},
        {"C.LI", 0x4415U, K::Addi, 8U, 0U, 0U, 5U},
        {"C.LUI", 0x6405U, K::Lui, 8U, 0U, 0U, 0x1000U},
        {"C.ADDI16SP", 0x717DU, K::Addi, 2U, 2U, 0U,
         0xFFFFFFFFFFFFFFF0ULL},
        {"C.SRLI 63", 0x907DU, K::Srli, 8U, 8U, 0U, 63U},
        {"C.SRAI 63", 0x947DU, K::Srai, 8U, 8U, 0U, 63U},
        {"C.ANDI", 0x880DU, K::Andi, 8U, 8U, 0U, 3U},
        {"C.SUB", 0x8C05U, K::Sub, 8U, 8U, 9U, 0U},
        {"C.XOR", 0x8C25U, K::Xor, 8U, 8U, 9U, 0U},
        {"C.OR", 0x8C45U, K::Or, 8U, 8U, 9U, 0U},
        {"C.AND", 0x8C65U, K::And, 8U, 8U, 9U, 0U},
        {"C.SUBW", 0x9C05U, K::Subw, 8U, 8U, 9U, 0U},
        {"C.ADDW", 0x9C25U, K::Addw, 8U, 8U, 9U, 0U},
        {"C.J", 0xA011U, K::Jal, 0U, 0U, 0U, 4U},
        {"C.BEQZ", 0xC011U, K::Beq, 0U, 8U, 0U, 4U},
        {"C.BNEZ", 0xE011U, K::Bne, 0U, 8U, 0U, 4U},
        {"C.SLLI 63", 0x147EU, K::Slli, 8U, 8U, 0U, 63U},
        {"C.LWSP", 0x42A2U, K::Lw, 5U, 2U, 0U, 8U},
        {"C.LDSP", 0x6282U, K::Ld, 5U, 2U, 0U, 0U},
        {"C.JR", 0x8282U, K::Jalr, 0U, 5U, 0U, 0U},
        {"C.MV", 0x8496U, K::Add, 9U, 0U, 5U, 0U},
        {"C.EBREAK", 0x9002U, K::Ebreak, 0U, 0U, 0U, 0U},
        {"C.JALR", 0x9282U, K::Jalr, 1U, 5U, 0U, 0U},
        {"C.ADD", 0x9426U, K::Add, 8U, 8U, 9U, 0U},
        {"C.SWSP", 0xC426U, K::Sw, 0U, 2U, 9U, 8U},
        {"C.SDSP", 0xE026U, K::Sd, 0U, 2U, 9U, 0U},
    };
    for (const auto& test_case : cases) {
        check_decode(test_case);
    }
}

void test_reserved_encodings()
{
    check_illegal(0x0000U); // C.ADDI4SPN with zero immediate
    check_illegal(0x2005U); // C.ADDIW rd=x0
    check_illegal(0x6101U); // C.ADDI16SP with zero immediate
    check_illegal(0x6181U); // C.LUI with zero immediate
    check_illegal(0x6005U); // C.LUI rd=x0
    check_illegal(0x9C41U); // RV64 reserved arithmetic sub-op
    check_illegal(0x4002U); // C.LWSP rd=x0
    check_illegal(0x6002U); // C.LDSP rd=x0
    check_illegal(0x8002U); // C.JR rs1=x0
    check_illegal(0x0003U); // standard instruction quadrant
    check_illegal(0x2000U); // C.FLD without floating point
    check_illegal(0xA000U); // C.FSD without floating point
}

class TestBus final : public rv::CpuBus {
  public:
    explicit TestBus(std::size_t size) : bytes_(size) {}

    [[nodiscard]] rv::ReadResult read(
        rv::PhysAddr address,
        rv::AccessWidth width,
        rv::AccessKind kind) override
    {
        static_cast<void>(kind);
        const std::uint64_t count = rv::width_bytes(width);
        if ((address & (count - 1U)) != 0U) {
            return {.fault = rv::BusFault::Misaligned};
        }
        if (address < base || count > bytes_.size() ||
            address - base > bytes_.size() - count) {
            return {.fault = rv::BusFault::Unmapped};
        }
        const std::size_t offset =
            static_cast<std::size_t>(address - base);
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < count; ++index) {
            value |= static_cast<std::uint64_t>(bytes_[offset + index])
                     << (index * 8U);
        }
        return {.fault = rv::BusFault::None, .value = value};
    }

    [[nodiscard]] rv::BusFault write(
        rv::PhysAddr address,
        rv::AccessWidth width,
        std::uint64_t value,
        rv::AccessKind kind) override
    {
        static_cast<void>(address);
        static_cast<void>(width);
        static_cast<void>(value);
        static_cast<void>(kind);
        return rv::BusFault::Unsupported;
    }

    [[nodiscard]] rv::ReadResult load_reserved_word(
        std::uint32_t hart_id,
        rv::PhysAddr address) override
    {
        static_cast<void>(hart_id);
        static_cast<void>(address);
        return {.fault = rv::BusFault::Unsupported};
    }

    [[nodiscard]] rv::StoreConditionalResult store_conditional_word(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        std::uint32_t value) override
    {
        static_cast<void>(hart_id);
        static_cast<void>(address);
        static_cast<void>(value);
        return {.fault = rv::BusFault::Unsupported};
    }

    [[nodiscard]] rv::AtomicResult atomic_word(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        rv::AmoOperation operation,
        std::uint32_t operand) override
    {
        static_cast<void>(hart_id);
        static_cast<void>(address);
        static_cast<void>(operation);
        static_cast<void>(operand);
        return {.fault = rv::BusFault::Unsupported};
    }

    [[nodiscard]] std::uint64_t read_time() const noexcept override
    {
        return 0;
    }

    void write_bytes(std::size_t offset, std::span<const std::uint8_t> bytes)
    {
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes_[offset + index] = bytes[index];
        }
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

void test_ialign_and_cross_word_fetch()
{
    TestBus bus(16U);
    constexpr std::array<std::uint8_t, 6> program{
        0x01U, 0x00U,             // C.NOP
        0x93U, 0x00U, 0x70U, 0x00U, // ADDI x1,x0,7
    };
    bus.write_bytes(0U, program);
    rv64::Core core(bus);
    core.reset({.reset_pc = base});
    const auto compressed = core.step();
    CHECK(compressed.status == rv64::StepStatus::Retired);
    CHECK(compressed.instruction == 0x0001U);
    CHECK(core.snapshot().pc == base + 2U);
    const auto standard = core.step();
    CHECK(standard.status == rv64::StepStatus::Retired);
    CHECK(standard.instruction == 0x00700093U);
    CHECK(core.snapshot().pc == base + 6U);
    CHECK(core.snapshot().registers[1] == 7U);

    core.reset({.reset_pc = base + 1U});
    CHECK(
        core.step().status ==
        rv64::StepStatus::TrapTaken);
}

void test_second_halfword_fault_and_illegal_raw()
{
    TestBus short_bus(4U);
    constexpr std::array<std::uint8_t, 2> first_half{0x13U, 0x01U};
    short_bus.write_bytes(2U, first_half);
    rv64::Core core(short_bus);
    core.reset({.reset_pc = base + 2U});
    const auto fault = core.step();
    CHECK(fault.status == rv64::StepStatus::TrapTaken);
    CHECK(fault.instruction == 0x0113U);
    CHECK(fault.trap_value == base + 4U);
    CHECK(fault.bus_fault == rv::BusFault::Unmapped);

    TestBus illegal_bus(4U);
    constexpr std::array<std::uint8_t, 2> zero{0U, 0U};
    illegal_bus.write_bytes(0U, zero);
    rv64::Core illegal_core(illegal_bus);
    illegal_core.reset({.reset_pc = base});
    const auto illegal = illegal_core.step();
    CHECK(illegal.status == rv64::StepStatus::TrapTaken);
    CHECK(illegal.instruction == 0U);
    CHECK(illegal.trap_value == 0U);
    CHECK(illegal_core.snapshot().pc == 0U);
    CHECK(
        illegal_core.snapshot().machine_csrs.mcause ==
        static_cast<std::uint64_t>(
            rv64::ExceptionCause::IllegalInstruction));
}

void test_cross_page_fetch()
{
    TestBus bus(4098U);
    constexpr std::array<std::uint8_t, 4> instruction{
        0x93U, 0x00U, 0xB0U, 0x02U, // ADDI x1,x0,43
    };
    bus.write_bytes(4094U, instruction);
    rv64::Core core(bus);
    core.reset({.reset_pc = base + 4094U});
    const auto result = core.step();
    CHECK(result.status == rv64::StepStatus::Retired);
    CHECK(result.instruction == 0x02B00093U);
    CHECK(core.snapshot().pc == base + 4098U);
    CHECK(core.snapshot().registers[1] == 43U);
}

} // namespace

int main()
{
    test_rv64c_decode();
    test_reserved_encodings();
    test_ialign_and_cross_word_fetch();
    test_second_halfword_fault_and_illegal_raw();
    test_cross_page_fetch();
    if (failures == 0) {
        std::cout << "All independent RV64C tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
