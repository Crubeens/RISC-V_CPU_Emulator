#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "rv/devices/ram.hpp"
#include "rv64/platform/machine.hpp"

namespace {

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd,
    std::uint32_t opcode = 0x13U) noexcept
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_s(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3) noexcept
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
    std::uint32_t funct3) noexcept
{
    return (((immediate >> 12U) & 0x1U) << 31U) |
           (((immediate >> 5U) & 0x3FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           (((immediate >> 1U) & 0xFU) << 8U) |
           (((immediate >> 11U) & 0x1U) << 7U) |
           0x63U;
}

template <std::size_t Size>
[[nodiscard]] constexpr std::array<std::uint8_t, Size * 4U>
to_bytes(const std::array<std::uint32_t, Size>& words) noexcept
{
    std::array<std::uint8_t, Size * 4U> bytes{};
    for (std::size_t word = 0; word < Size; ++word) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            bytes[word * 4U + byte] = static_cast<std::uint8_t>(
                words[word] >> (byte * 8U));
        }
    }
    return bytes;
}

[[nodiscard]] bool same_result(
    const rv64::StepResult& lhs,
    const rv64::StepResult& rhs) noexcept
{
    return lhs.status == rhs.status &&
           lhs.privilege == rhs.privilege &&
           lhs.pc == rhs.pc &&
           lhs.instruction == rhs.instruction &&
           lhs.trap_value == rhs.trap_value &&
           lhs.bus_fault == rhs.bus_fault &&
           lhs.register_write.enabled == rhs.register_write.enabled &&
           lhs.register_write.index == rhs.register_write.index &&
           lhs.register_write.value == rhs.register_write.value;
}

} // namespace

int main()
{
    constexpr std::array<std::uint32_t, 8> program{
        0x00000197U, // auipc x3, 0
        encode_i(0U, 0U, 0U, 1U),
        encode_i(16U, 0U, 0U, 2U),
        encode_i(1U, 1U, 0U, 1U),
        encode_s(128U, 1U, 3U, 3U),
        encode_i(128U, 3U, 3U, 4U, 0x03U),
        encode_b(static_cast<std::uint32_t>(-12), 2U, 1U, 4U),
        0x0000006FU, // jal x0, 0
    };
    constexpr auto image = to_bytes(program);

    const rv64::platform::MachineConfig config{
        .ram_size = 1024U * 1024U,
        .virtual_disk_size = 512U,
        .enable_framebuffer = false,
    };
    rv64::platform::Machine reference(config);
    rv64::platform::Machine fast(config);
    if (reference.load_image(
            image,
            rv64::platform::address_map::dram_base) !=
            rv::BusFault::None ||
        fast.load_image(
            image,
            rv64::platform::address_map::dram_base) !=
            rv::BusFault::None) {
        std::cerr << "cannot load RV64 mode-differential image\n";
        return 1;
    }

    reference.core().set_execution_mode(
        rv64::ExecutionMode::Reference);
    reference.reset({
        .reset_pc = rv64::platform::address_map::dram_base,
    });
    fast.reset({
        .reset_pc = rv64::platform::address_map::dram_base,
    });

    constexpr std::uint64_t steps = 300U;
    for (std::uint64_t step = 0; step < steps; ++step) {
        const auto reference_result = reference.step();
        const auto fast_result = fast.step();
        if (!same_result(reference_result, fast_result) ||
            reference.core().snapshot() != fast.core().snapshot()) {
            std::cerr
                << "RV64 reference/fast divergence at step "
                << step << '\n';
            return 1;
        }
    }

    const auto& reference_counters =
        reference.core().performance_counters();
    const auto& fast_counters =
        fast.core().performance_counters();
    if (reference_counters.decode.lookups != 0U ||
        fast_counters.decode.hits == 0U ||
        fast_counters.decode.misses == 0U ||
        fast.core().snapshot().registers[1] != 16U ||
        fast.core().snapshot().registers[4] != 16U) {
        std::cerr << "RV64 mode-differential coverage was incomplete\n";
        return 1;
    }

    std::cout
        << "RV64 reference/fast differential passed for "
        << steps << " commits\n";
    return 0;
}
