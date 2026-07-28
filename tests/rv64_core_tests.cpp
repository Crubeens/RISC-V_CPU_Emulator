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

namespace {

constexpr std::uint64_t base = 0x80000000ULL;
constexpr std::size_t memory_size = 64U * 1024U;
int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

class TestBus final : public rv::CpuBus {
  public:
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
        if (address < base ||
            address - base > bytes_.size() - count) {
            return {.fault = rv::BusFault::Unmapped};
        }
        const std::size_t offset =
            static_cast<std::size_t>(address - base);
        std::uint64_t value = 0;
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(count);
             ++index) {
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
        static_cast<void>(kind);
        const std::uint64_t count = rv::width_bytes(width);
        if ((address & (count - 1U)) != 0U) {
            return rv::BusFault::Misaligned;
        }
        if (address < base ||
            address - base > bytes_.size() - count) {
            return rv::BusFault::Unmapped;
        }
        const std::size_t offset =
            static_cast<std::size_t>(address - base);
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(count);
             ++index) {
            bytes_[offset + index] = static_cast<std::uint8_t>(
                value >> (index * 8U));
        }
        return rv::BusFault::None;
    }

    [[nodiscard]] rv::ReadResult load_reserved_word(
        std::uint32_t hart_id,
        rv::PhysAddr address) override
    {
        static_cast<void>(hart_id);
        return read(address, rv::AccessWidth::Word, rv::AccessKind::Atomic);
    }

    [[nodiscard]] rv::StoreConditionalResult store_conditional_word(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        std::uint32_t value) override
    {
        static_cast<void>(hart_id);
        return {
            .fault = write(
                address,
                rv::AccessWidth::Word,
                value,
                rv::AccessKind::Atomic),
            .succeeded = true,
        };
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

    void load_program(std::span<const std::uint32_t> program)
    {
        bytes_.fill(0);
        for (std::size_t index = 0; index < program.size(); ++index) {
            const std::uint32_t instruction = program[index];
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                bytes_[index * 4U + byte] =
                    static_cast<std::uint8_t>(
                        instruction >> (byte * 8U));
            }
        }
    }

  private:
    std::array<std::uint8_t, memory_size> bytes_{};
};

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd,
    std::uint32_t opcode = 0x13U)
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_r(
    std::uint32_t funct7,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd,
    std::uint32_t opcode = 0x33U)
{
    return ((funct7 & 0x7FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_s(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3)
{
    return (((immediate >> 5U) & 0x7FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((immediate & 0x1FU) << 7U) |
           0x23U;
}

[[nodiscard]] constexpr std::uint32_t encode_b(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3)
{
    return (((immediate >> 12U) & 1U) << 31U) |
           (((immediate >> 5U) & 0x3FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           (((immediate >> 1U) & 0xFU) << 8U) |
           (((immediate >> 11U) & 1U) << 7U) |
           0x63U;
}

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd,
    std::uint32_t opcode)
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_j(
    std::uint32_t immediate,
    std::uint32_t rd)
{
    return (((immediate >> 20U) & 1U) << 31U) |
           (((immediate >> 1U) & 0x3FFU) << 21U) |
           (((immediate >> 11U) & 1U) << 20U) |
           (((immediate >> 12U) & 0xFFU) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x6FU;
}

void test_decode_complete_rv64i()
{
    using K = rv64::InstructionKind;
    const std::pair<std::uint32_t, K> cases[]{
        {encode_u(0x80000000U, 1U, 0x37U), K::Lui},
        {encode_u(0x1000U, 1U, 0x17U), K::Auipc},
        {encode_j(8U, 1U), K::Jal},
        {encode_i(0U, 1U, 0U, 2U, 0x67U), K::Jalr},
        {encode_b(8U, 2U, 1U, 0U), K::Beq},
        {encode_b(8U, 2U, 1U, 1U), K::Bne},
        {encode_b(8U, 2U, 1U, 4U), K::Blt},
        {encode_b(8U, 2U, 1U, 5U), K::Bge},
        {encode_b(8U, 2U, 1U, 6U), K::Bltu},
        {encode_b(8U, 2U, 1U, 7U), K::Bgeu},
        {encode_i(0U, 1U, 0U, 2U, 0x03U), K::Lb},
        {encode_i(0U, 1U, 1U, 2U, 0x03U), K::Lh},
        {encode_i(0U, 1U, 2U, 2U, 0x03U), K::Lw},
        {encode_i(0U, 1U, 3U, 2U, 0x03U), K::Ld},
        {encode_i(0U, 1U, 4U, 2U, 0x03U), K::Lbu},
        {encode_i(0U, 1U, 5U, 2U, 0x03U), K::Lhu},
        {encode_i(0U, 1U, 6U, 2U, 0x03U), K::Lwu},
        {encode_s(0U, 2U, 1U, 0U), K::Sb},
        {encode_s(0U, 2U, 1U, 1U), K::Sh},
        {encode_s(0U, 2U, 1U, 2U), K::Sw},
        {encode_s(0U, 2U, 1U, 3U), K::Sd},
        {encode_i(1U, 1U, 0U, 2U), K::Addi},
        {encode_i(1U, 1U, 2U, 2U), K::Slti},
        {encode_i(1U, 1U, 3U, 2U), K::Sltiu},
        {encode_i(1U, 1U, 4U, 2U), K::Xori},
        {encode_i(1U, 1U, 6U, 2U), K::Ori},
        {encode_i(1U, 1U, 7U, 2U), K::Andi},
        {encode_i(63U, 1U, 1U, 2U), K::Slli},
        {encode_i(63U, 1U, 5U, 2U), K::Srli},
        {encode_i(0x43FU, 1U, 5U, 2U), K::Srai},
        {encode_i(1U, 1U, 0U, 2U, 0x1BU), K::Addiw},
        {encode_i(31U, 1U, 1U, 2U, 0x1BU), K::Slliw},
        {encode_i(31U, 1U, 5U, 2U, 0x1BU), K::Srliw},
        {encode_i(0x41FU, 1U, 5U, 2U, 0x1BU), K::Sraiw},
        {encode_r(0U, 2U, 1U, 0U, 3U), K::Add},
        {encode_r(0x20U, 2U, 1U, 0U, 3U), K::Sub},
        {encode_r(0U, 2U, 1U, 1U, 3U), K::Sll},
        {encode_r(0U, 2U, 1U, 2U, 3U), K::Slt},
        {encode_r(0U, 2U, 1U, 3U, 3U), K::Sltu},
        {encode_r(0U, 2U, 1U, 4U, 3U), K::Xor},
        {encode_r(0U, 2U, 1U, 5U, 3U), K::Srl},
        {encode_r(0x20U, 2U, 1U, 5U, 3U), K::Sra},
        {encode_r(0U, 2U, 1U, 6U, 3U), K::Or},
        {encode_r(0U, 2U, 1U, 7U, 3U), K::And},
        {encode_r(0U, 2U, 1U, 0U, 3U, 0x3BU), K::Addw},
        {encode_r(0x20U, 2U, 1U, 0U, 3U, 0x3BU), K::Subw},
        {encode_r(0U, 2U, 1U, 1U, 3U, 0x3BU), K::Sllw},
        {encode_r(0U, 2U, 1U, 5U, 3U, 0x3BU), K::Srlw},
        {encode_r(0x20U, 2U, 1U, 5U, 3U, 0x3BU), K::Sraw},
        {0x0000000FU, K::Fence},
        {0x0000100FU, K::FenceI},
        {0x00000073U, K::Ecall},
        {0x00100073U, K::Ebreak},
    };

    for (const auto& [instruction, expected] : cases) {
        CHECK(rv64::decode_instruction(instruction).kind == expected);
    }
    CHECK(!rv64::decode_instruction(0U).valid());
    CHECK(!rv64::decode_instruction(
               encode_i(0x800U, 1U, 1U, 2U))
               .valid());
}

[[nodiscard]] rv64::CpuSnapshot run_program(
    TestBus& bus,
    std::span<const std::uint32_t> program)
{
    bus.load_program(program);
    rv64::Core core(bus);
    core.reset({.reset_pc = base});
    for (std::size_t index = 0; index < program.size(); ++index) {
        const auto result = core.step();
        CHECK(result.status == rv64::StepStatus::Retired);
    }
    return core.snapshot();
}

void test_integer_and_word_execution()
{
    TestBus bus;
    const std::uint32_t program[]{
        encode_i(0xFFFU, 0U, 0U, 1U),
        encode_i(63U, 1U, 1U, 2U),
        encode_i(63U, 2U, 5U, 3U),
        encode_i(0x43FU, 2U, 5U, 4U),
        encode_i(0U, 1U, 2U, 5U),
        encode_i(1U, 1U, 3U, 6U),
        encode_i(0xFFFU, 0U, 0U, 7U, 0x1BU),
        encode_i(31U, 7U, 1U, 8U, 0x1BU),
        encode_i(31U, 8U, 5U, 9U, 0x1BU),
        encode_i(0x41FU, 8U, 5U, 10U, 0x1BU),
        encode_r(0U, 3U, 2U, 0U, 11U),
        encode_r(0x20U, 3U, 2U, 0U, 12U),
        encode_r(0U, 3U, 2U, 1U, 13U),
        encode_r(0U, 3U, 2U, 2U, 14U),
        encode_r(0U, 3U, 2U, 3U, 15U),
        encode_r(0U, 3U, 2U, 4U, 16U),
        encode_r(0U, 3U, 2U, 5U, 17U),
        encode_r(0x20U, 3U, 2U, 5U, 18U),
        encode_r(0U, 3U, 2U, 6U, 19U),
        encode_r(0U, 3U, 2U, 7U, 20U),
        encode_r(0U, 3U, 8U, 0U, 21U, 0x3BU),
        encode_r(0x20U, 3U, 8U, 0U, 22U, 0x3BU),
        encode_r(0U, 3U, 8U, 1U, 23U, 0x3BU),
        encode_r(0U, 3U, 8U, 5U, 24U, 0x3BU),
        encode_r(0x20U, 3U, 8U, 5U, 25U, 0x3BU),
        encode_u(0x80000000U, 26U, 0x37U),
        encode_i(123U, 0U, 0U, 0U),
    };
    const auto state = run_program(bus, program);
    CHECK(state.registers[0] == 0U);
    CHECK(state.registers[1] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[2] == 0x8000000000000000ULL);
    CHECK(state.registers[3] == 1U);
    CHECK(state.registers[4] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[5] == 1U);
    CHECK(state.registers[6] == 0U);
    CHECK(state.registers[7] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[8] == 0xFFFFFFFF80000000ULL);
    CHECK(state.registers[9] == 1U);
    CHECK(state.registers[10] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[26] == 0xFFFFFFFF80000000ULL);
    CHECK(state.instructions_retired == std::size(program));
}

void test_immediate_and_overflow_boundaries()
{
    TestBus bus;
    const std::uint32_t program[]{
        encode_i(0xFFFU, 0U, 0U, 1U),
        encode_i(0xFFFU, 1U, 4U, 2U),
        encode_i(0xFFFU, 0U, 6U, 3U),
        encode_i(0x7FFU, 3U, 7U, 4U),
        encode_i(1U, 0U, 0U, 5U),
        encode_i(63U, 5U, 1U, 6U),
        encode_i(63U, 6U, 5U, 7U),
        encode_i(0x43FU, 6U, 5U, 8U),
        encode_r(0U, 5U, 3U, 0U, 9U),
        encode_r(0x20U, 5U, 0U, 0U, 10U),
        encode_u(0x80000000U, 11U, 0x37U),
        encode_i(0xFFFU, 11U, 0U, 12U, 0x1BU),
        encode_i(1U, 12U, 0U, 13U, 0x1BU),
        0x0000000FU,
        0x0000100FU,
    };
    const auto state = run_program(bus, program);
    CHECK(state.registers[2] == 0U);
    CHECK(state.registers[3] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[4] == 0x7FFU);
    CHECK(state.registers[6] == 0x8000000000000000ULL);
    CHECK(state.registers[7] == 1U);
    CHECK(state.registers[8] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[9] == 0U);
    CHECK(state.registers[10] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[12] == 0x000000007FFFFFFFULL);
    CHECK(state.registers[13] == 0xFFFFFFFF80000000ULL);
}

void test_doubleword_and_sign_extending_loads()
{
    TestBus bus;
    const std::uint32_t program[]{
        encode_u(0x1000U, 1U, 0x17U),
        encode_i(0xFFFU, 0U, 0U, 2U),
        encode_s(0U, 2U, 1U, 3U),
        encode_i(0U, 1U, 3U, 3U, 0x03U),
        encode_s(8U, 2U, 1U, 2U),
        encode_i(8U, 1U, 2U, 4U, 0x03U),
        encode_i(8U, 1U, 6U, 5U, 0x03U),
        encode_s(12U, 2U, 1U, 1U),
        encode_i(12U, 1U, 1U, 6U, 0x03U),
        encode_i(12U, 1U, 5U, 7U, 0x03U),
        encode_s(14U, 2U, 1U, 0U),
        encode_i(14U, 1U, 0U, 8U, 0x03U),
        encode_i(14U, 1U, 4U, 9U, 0x03U),
    };
    const auto state = run_program(bus, program);
    CHECK(state.registers[1] == base + 0x1000U);
    CHECK(state.registers[3] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[4] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[5] == 0x00000000FFFFFFFFULL);
    CHECK(state.registers[6] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[7] == 0xFFFFU);
    CHECK(state.registers[8] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.registers[9] == 0xFFU);
}

void test_control_flow_and_exceptions()
{
    TestBus bus;
    const std::uint32_t program[]{
        encode_i(0xFFFU, 0U, 0U, 1U),
        encode_i(1U, 0U, 0U, 2U),
        encode_b(8U, 2U, 1U, 4U),
        encode_i(99U, 0U, 0U, 3U),
        encode_i(7U, 0U, 0U, 3U),
        encode_b(8U, 2U, 1U, 6U),
        encode_i(5U, 0U, 0U, 4U),
        encode_j(8U, 5U),
        encode_i(99U, 0U, 0U, 4U),
        encode_i(9U, 0U, 0U, 6U),
    };
    bus.load_program(program);
    rv64::Core core(bus);
    core.reset({.reset_pc = base});
    for (int index = 0; index < 8; ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    const auto state = core.snapshot();
    CHECK(state.registers[3] == 7U);
    CHECK(state.registers[4] == 5U);
    CHECK(state.registers[5] == base + 8U * 4U);
    CHECK(state.registers[6] == 9U);

    const std::uint32_t illegal[]{0U};
    bus.load_program(illegal);
    core.reset({.reset_pc = base});
    CHECK(core.step().status == rv64::StepStatus::IllegalInstruction);
    CHECK(core.snapshot().pc == base);
    CHECK(core.snapshot().instructions_retired == 0U);

    core.reset({.reset_pc = base + 2U});
    CHECK(
        core.step().status ==
        rv64::StepStatus::InstructionAddressMisaligned);

    const std::uint32_t bad_jump[]{encode_j(2U, 1U)};
    bus.load_program(bad_jump);
    core.reset({.reset_pc = base});
    CHECK(
        core.step().status ==
        rv64::StepStatus::InstructionAddressMisaligned);
    CHECK(core.snapshot().registers[1] == 0U);

    const std::uint32_t bad_load[]{
        encode_i(1U, 0U, 0U, 1U),
        encode_i(0U, 1U, 3U, 2U, 0x03U),
    };
    bus.load_program(bad_load);
    core.reset({.reset_pc = base});
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::LoadAddressMisaligned);

    const std::uint32_t bad_store[]{
        encode_i(1U, 0U, 0U, 1U),
        encode_s(0U, 0U, 1U, 3U),
    };
    bus.load_program(bad_store);
    core.reset({.reset_pc = base});
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::StoreAddressMisaligned);
}

void test_all_branches_jalr_and_commit_metadata()
{
    TestBus bus;
    const std::uint32_t program[]{
        encode_i(0xFFFU, 0U, 0U, 1U),
        encode_i(1U, 0U, 0U, 2U),
        encode_b(8U, 2U, 2U, 0U),
        encode_i(1U, 0U, 0U, 10U),
        encode_b(8U, 2U, 1U, 1U),
        encode_i(1U, 0U, 0U, 11U),
        encode_b(8U, 2U, 1U, 4U),
        encode_i(1U, 0U, 0U, 12U),
        encode_b(8U, 1U, 2U, 5U),
        encode_i(1U, 0U, 0U, 13U),
        encode_b(8U, 1U, 2U, 6U),
        encode_i(1U, 0U, 0U, 14U),
        encode_b(8U, 2U, 1U, 7U),
        encode_i(1U, 0U, 0U, 15U),
        encode_u(0U, 3U, 0x17U),
        encode_i(16U, 3U, 0U, 3U),
        encode_i(0U, 3U, 0U, 4U, 0x67U),
        encode_i(1U, 0U, 0U, 16U),
        encode_i(9U, 0U, 0U, 17U),
    };
    bus.load_program(program);
    rv64::Core core(bus);
    core.reset({.reset_pc = base});
    for (int index = 0; index < 12; ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    const auto state = core.snapshot();
    for (std::size_t index = 10U; index <= 16U; ++index) {
        CHECK(state.registers[index] == 0U);
    }
    CHECK(state.registers[4] == base + 17U * 4U);
    CHECK(state.registers[17] == 9U);

    const std::uint32_t commits[]{
        encode_i(3U, 0U, 0U, 1U),
        encode_i(7U, 0U, 0U, 0U),
    };
    bus.load_program(commits);
    core.reset({.reset_pc = base});
    const auto first = core.step();
    CHECK(first.register_write.enabled);
    CHECK(first.register_write.index == 1U);
    CHECK(first.register_write.value == 3U);
    const auto second = core.step();
    CHECK(!second.register_write.enabled);
    CHECK(core.snapshot().registers[0] == 0U);
}

void test_environment_and_access_faults()
{
    TestBus bus;
    rv64::Core core(bus);

    const std::uint32_t environment[]{0x00000073U, 0x00100073U};
    bus.load_program(environment);
    core.reset({.reset_pc = base});
    CHECK(core.step().status == rv64::StepStatus::EnvironmentCall);
    CHECK(core.snapshot().pc == base);

    bus.load_program(
        std::span<const std::uint32_t>(environment).subspan(1U));
    core.reset({.reset_pc = base});
    CHECK(core.step().status == rv64::StepStatus::Breakpoint);
    CHECK(core.snapshot().pc == base);

    core.reset({.reset_pc = base + memory_size});
    const auto fetch = core.step();
    CHECK(fetch.status == rv64::StepStatus::InstructionAccessFault);
    CHECK(fetch.bus_fault == rv::BusFault::Unmapped);

    const std::uint32_t bad_load[]{
        encode_i(0U, 0U, 3U, 1U, 0x03U),
    };
    bus.load_program(bad_load);
    core.reset({.reset_pc = base});
    const auto load = core.step();
    CHECK(load.status == rv64::StepStatus::LoadAccessFault);
    CHECK(load.bus_fault == rv::BusFault::Unmapped);
    CHECK(load.trap_value == 0U);

    const std::uint32_t bad_store[]{
        encode_s(0U, 0U, 0U, 3U),
    };
    bus.load_program(bad_store);
    core.reset({.reset_pc = base});
    const auto store = core.step();
    CHECK(store.status == rv64::StepStatus::StoreAccessFault);
    CHECK(store.bus_fault == rv::BusFault::Unmapped);
}

} // namespace

int main()
{
    test_decode_complete_rv64i();
    test_integer_and_word_execution();
    test_immediate_and_overflow_boundaries();
    test_doubleword_and_sign_extending_loads();
    test_control_flow_and_exceptions();
    test_all_branches_jalr_and_commit_metadata();
    test_environment_and_access_faults();

    if (failures == 0) {
        std::cout << "All independent RV64I core tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
