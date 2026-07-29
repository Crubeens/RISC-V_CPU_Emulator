#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <utility>

#include "rv64/core/decode.hpp"
#include "rv64/core/trap.hpp"
#include "rv64/platform/machine.hpp"

namespace {

constexpr std::uint64_t base = rv64::platform::address_map::dram_base;
constexpr std::uint64_t data_address = base + 0x1000U;
int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd,
    std::uint32_t opcode = 0x03U)
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_atomic(
    std::uint32_t funct5,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t rd,
    bool doubleword,
    bool acquire = false,
    bool release = false)
{
    return ((funct5 & 0x1FU) << 27U) |
           (static_cast<std::uint32_t>(acquire) << 26U) |
           (static_cast<std::uint32_t>(release) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((doubleword ? 3U : 2U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x2FU;
}

void load_program(
    rv64::platform::Machine& machine,
    std::span<const std::uint32_t> program)
{
    std::array<std::uint8_t, 128> bytes{};
    CHECK(program.size() * 4U <= bytes.size());
    for (std::size_t word = 0; word < program.size(); ++word) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            bytes[word * 4U + byte] = static_cast<std::uint8_t>(
                program[word] >> (byte * 8U));
        }
    }
    CHECK(
        machine.load_image(
            std::span(bytes).first(program.size() * 4U),
            base) == rv::BusFault::None);
    machine.reset({.reset_pc = base});
}

[[nodiscard]] rv64::platform::Machine make_machine()
{
    return rv64::platform::Machine({
        .ram_size = 1ULL * 1024ULL * 1024ULL,
        .virtual_disk_size = 512ULL,
        .enable_framebuffer = false,
    });
}

struct AmoResult {
    std::uint64_t returned{};
    std::uint64_t memory{};
};

[[nodiscard]] AmoResult execute_amo(
    std::uint32_t funct5,
    bool doubleword,
    std::uint64_t initial,
    std::uint64_t operand)
{
    auto machine = make_machine();
    const rv::AccessWidth width =
        doubleword ? rv::AccessWidth::DoubleWord : rv::AccessWidth::Word;
    CHECK(
        machine.bus().dma_write(data_address, width, initial) ==
        rv::BusFault::None);
    CHECK(
        machine.bus().dma_write(
            data_address + 16U,
            rv::AccessWidth::DoubleWord,
            operand) == rv::BusFault::None);
    const std::uint32_t program[]{
        0x00001097U, // auipc x1,0x1000
        encode_i(16U, 1U, 3U, 2U), // ld x2,16(x1)
        encode_atomic(funct5, 2U, 1U, 3U, doubleword),
    };
    load_program(machine, program);
    for (int index = 0; index < 3; ++index) {
        CHECK(machine.step().status == rv64::StepStatus::Retired);
    }
    const auto memory = machine.bus().dma_read(data_address, width);
    CHECK(memory.ok());
    return {
        .returned = machine.core().snapshot().registers[3],
        .memory = memory.value,
    };
}

void test_decode_and_ordering_bits()
{
    using K = rv64::InstructionKind;
    constexpr std::pair<std::uint32_t, K> word_cases[]{
        {0x02U, K::LrW},      {0x03U, K::ScW},
        {0x01U, K::AmoSwapW}, {0x00U, K::AmoAddW},
        {0x04U, K::AmoXorW},  {0x0CU, K::AmoAndW},
        {0x08U, K::AmoOrW},   {0x10U, K::AmoMinW},
        {0x14U, K::AmoMaxW},  {0x18U, K::AmoMinuW},
        {0x1CU, K::AmoMaxuW},
    };
    constexpr std::pair<std::uint32_t, K> double_cases[]{
        {0x02U, K::LrD},      {0x03U, K::ScD},
        {0x01U, K::AmoSwapD}, {0x00U, K::AmoAddD},
        {0x04U, K::AmoXorD},  {0x0CU, K::AmoAndD},
        {0x08U, K::AmoOrD},   {0x10U, K::AmoMinD},
        {0x14U, K::AmoMaxD},  {0x18U, K::AmoMinuD},
        {0x1CU, K::AmoMaxuD},
    };
    for (const auto& [funct5, expected] : word_cases) {
        const auto decoded = rv64::decode_instruction(
            encode_atomic(funct5, 0U, 1U, 2U, false, true, true));
        CHECK(decoded.kind == expected);
        CHECK(decoded.acquire);
        CHECK(decoded.release);
    }
    for (const auto& [funct5, expected] : double_cases) {
        CHECK(
            rv64::decode_instruction(
                encode_atomic(funct5, 0U, 1U, 2U, true)).kind ==
            expected);
    }
    CHECK(
        !rv64::decode_instruction(
             encode_atomic(0x02U, 2U, 1U, 3U, false)).valid());
    CHECK(!rv64::decode_instruction(0x0000102FU).valid());
}

void test_all_word_amos_and_sign_extension()
{
    constexpr std::uint64_t initial = 0x80000005U;
    constexpr std::uint64_t operand = 3U;
    constexpr std::pair<std::uint32_t, std::uint32_t> cases[]{
        {0x01U, 3U},
        {0x00U, 0x80000008U},
        {0x04U, 0x80000006U},
        {0x0CU, 1U},
        {0x08U, 0x80000007U},
        {0x10U, 0x80000005U},
        {0x14U, 3U},
        {0x18U, 3U},
        {0x1CU, 0x80000005U},
    };
    for (const auto& [funct5, expected] : cases) {
        const auto result =
            execute_amo(funct5, false, initial, operand);
        CHECK(result.returned == 0xFFFFFFFF80000005ULL);
        CHECK(result.memory == expected);
    }
    const auto wrapped = execute_amo(
        0x00U,
        false,
        std::numeric_limits<std::uint32_t>::max(),
        1U);
    CHECK(wrapped.returned == std::numeric_limits<std::uint64_t>::max());
    CHECK(wrapped.memory == 0U);
}

void test_all_doubleword_amos()
{
    constexpr std::uint64_t initial = 0x8000000000000005ULL;
    constexpr std::uint64_t operand = 3U;
    constexpr std::pair<std::uint32_t, std::uint64_t> cases[]{
        {0x01U, 3U},
        {0x00U, 0x8000000000000008ULL},
        {0x04U, 0x8000000000000006ULL},
        {0x0CU, 1U},
        {0x08U, 0x8000000000000007ULL},
        {0x10U, 0x8000000000000005ULL},
        {0x14U, 3U},
        {0x18U, 3U},
        {0x1CU, 0x8000000000000005ULL},
    };
    for (const auto& [funct5, expected] : cases) {
        const auto result =
            execute_amo(funct5, true, initial, operand);
        CHECK(result.returned == initial);
        CHECK(result.memory == expected);
    }
    const auto wrapped = execute_amo(
        0x00U,
        true,
        std::numeric_limits<std::uint64_t>::max(),
        1U);
    CHECK(wrapped.returned == std::numeric_limits<std::uint64_t>::max());
    CHECK(wrapped.memory == 0U);
}

void test_lr_sc_success_failure_and_width()
{
    auto machine = make_machine();
    CHECK(
        machine.bus().dma_write(
            data_address,
            rv::AccessWidth::DoubleWord,
            0x1122334455667788ULL) == rv::BusFault::None);
    CHECK(
        machine.bus().dma_write(
            data_address + 16U,
            rv::AccessWidth::DoubleWord,
            0x8877665544332211ULL) == rv::BusFault::None);
    const std::uint32_t program[]{
        0x00001097U,
        encode_i(16U, 1U, 3U, 2U),
        encode_atomic(0x02U, 0U, 1U, 3U, true, true, false),
        encode_atomic(0x03U, 2U, 1U, 4U, true, false, true),
        encode_atomic(0x03U, 2U, 1U, 5U, true),
        encode_atomic(0x02U, 0U, 1U, 6U, false),
        encode_atomic(0x03U, 2U, 1U, 7U, true),
    };
    load_program(machine, program);
    for (std::size_t index = 0; index < std::size(program); ++index) {
        CHECK(machine.step().status == rv64::StepStatus::Retired);
    }
    const auto state = machine.core().snapshot();
    CHECK(state.registers[3] == 0x1122334455667788ULL);
    CHECK(state.registers[4] == 0U);
    CHECK(state.registers[5] == 1U);
    CHECK(state.registers[6] == 0x0000000044332211ULL);
    CHECK(state.registers[7] == 1U);
    const auto memory = machine.bus().dma_read(
        data_address,
        rv::AccessWidth::DoubleWord);
    CHECK(memory.ok());
    CHECK(memory.value == 0x8877665544332211ULL);
}

void test_reservation_invalidation_and_address_change()
{
    auto machine = make_machine();
    CHECK(
        machine.bus().dma_write(
            data_address,
            rv::AccessWidth::DoubleWord,
            10U) == rv::BusFault::None);
    CHECK(
        machine.bus().dma_write(
            data_address + 16U,
            rv::AccessWidth::DoubleWord,
            20U) == rv::BusFault::None);
    const std::uint32_t program[]{
        0x00001097U,
        encode_i(16U, 1U, 3U, 2U),
        encode_atomic(0x02U, 0U, 1U, 3U, true),
        encode_atomic(0x01U, 2U, 1U, 0U, true),
        encode_atomic(0x03U, 2U, 1U, 4U, true),
        encode_atomic(0x02U, 0U, 1U, 5U, true),
        encode_i(8U, 1U, 0U, 1U, 0x13U),
        encode_atomic(0x03U, 2U, 1U, 6U, true),
    };
    load_program(machine, program);
    for (std::size_t index = 0; index < std::size(program); ++index) {
        CHECK(machine.step().status == rv64::StepStatus::Retired);
    }
    const auto state = machine.core().snapshot();
    CHECK(state.registers[4] == 1U);
    CHECK(state.registers[6] == 1U);
}

void test_dma_and_reset_invalidate_reservations()
{
    auto machine = make_machine();
    CHECK(
        machine.bus().dma_write(
            data_address,
            rv::AccessWidth::DoubleWord,
            10U) == rv::BusFault::None);
    CHECK(machine.bus().load_reserved_doubleword(0U, data_address).ok());
    CHECK(
        machine.bus().dma_write(
            data_address,
            rv::AccessWidth::DoubleWord,
            11U) == rv::BusFault::None);
    const auto after_dma = machine.bus().store_conditional_doubleword(
        0U,
        data_address,
        12U);
    CHECK(after_dma.ok());
    CHECK(!after_dma.succeeded);

    CHECK(machine.bus().load_reserved_doubleword(0U, data_address).ok());
    machine.reset({.reset_pc = base});
    const auto after_reset = machine.bus().store_conditional_doubleword(
        0U,
        data_address,
        13U);
    CHECK(after_reset.ok());
    CHECK(!after_reset.succeeded);
}

void test_atomic_faults()
{
    auto machine = make_machine();
    const std::uint32_t misaligned_lr[]{
        encode_i(1U, 0U, 0U, 1U, 0x13U),
        encode_atomic(0x02U, 0U, 1U, 2U, true),
    };
    load_program(machine, misaligned_lr);
    CHECK(machine.step().status == rv64::StepStatus::Retired);
    CHECK(
        machine.step().status ==
        rv64::StepStatus::TrapTaken);
    CHECK(
        machine.core().snapshot().machine_csrs.mcause ==
        static_cast<std::uint64_t>(
            rv64::ExceptionCause::LoadAddressMisaligned));

    const std::uint32_t misaligned_sc[]{
        encode_i(1U, 0U, 0U, 1U, 0x13U),
        encode_atomic(0x03U, 0U, 1U, 2U, false),
    };
    load_program(machine, misaligned_sc);
    CHECK(machine.step().status == rv64::StepStatus::Retired);
    CHECK(
        machine.step().status ==
        rv64::StepStatus::TrapTaken);
    CHECK(
        machine.core().snapshot().machine_csrs.mcause ==
        static_cast<std::uint64_t>(
            rv64::ExceptionCause::StoreAddressMisaligned));

    const std::uint32_t unmapped_lr[]{
        encode_atomic(0x02U, 0U, 0U, 2U, false),
    };
    load_program(machine, unmapped_lr);
    const auto load = machine.step();
    CHECK(load.status == rv64::StepStatus::TrapTaken);
    CHECK(load.bus_fault == rv::BusFault::Unmapped);

    const std::uint32_t unmapped_amo[]{
        encode_atomic(0x00U, 0U, 0U, 2U, true),
    };
    load_program(machine, unmapped_amo);
    const auto store = machine.step();
    CHECK(store.status == rv64::StepStatus::TrapTaken);
    CHECK(store.bus_fault == rv::BusFault::Unmapped);

    const std::uint32_t unmapped_sc[]{
        encode_atomic(0x03U, 0U, 0U, 2U, true),
    };
    load_program(machine, unmapped_sc);
    const auto conditional = machine.step();
    CHECK(conditional.status == rv64::StepStatus::TrapTaken);
    CHECK(conditional.bus_fault == rv::BusFault::Unmapped);
}

} // namespace

int main()
{
    test_decode_and_ordering_bits();
    test_all_word_amos_and_sign_extension();
    test_all_doubleword_amos();
    test_lr_sc_success_failure_and_width();
    test_reservation_invalidation_and_address_change();
    test_dma_and_reset_invalidate_reservations();
    test_atomic_faults();
    if (failures == 0) {
        std::cout << "All independent RV64A tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
