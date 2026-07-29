#include <array>
#include <cstdint>
#include <iostream>

#include "rv32/devices/uart16550.hpp"
#include "rv32/devices/ram.hpp"
#include "rv64/platform/machine.hpp"

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

void test_machine_executes_from_shared_physical_bus()
{
    rv64::platform::Machine machine({
        .ram_size = 1ULL * 1024ULL * 1024ULL,
        .virtual_disk_size = 512ULL,
        .enable_framebuffer = false,
    });
    // addi x1,x0,-1; sd x1,16(x0) would address zero, so first build
    // the DRAM base with AUIPC and use a local offset.
    constexpr std::array<std::uint32_t, 4> program{
        0x00000097U, // auipc x1,0
        0xFFF00113U, // addi x2,x0,-1
        0x0020B823U, // sd x2,16(x1)
        0x0100B183U, // ld x3,16(x1)
    };
    std::array<std::uint8_t, program.size() * 4U> image{};
    for (std::size_t word = 0; word < program.size(); ++word) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            image[word * 4U + byte] = static_cast<std::uint8_t>(
                program[word] >> (byte * 8U));
        }
    }
    CHECK(
        machine.load_image(
            image,
            rv64::platform::address_map::dram_base) ==
        rv::BusFault::None);
    machine.reset({
        .reset_pc = rv64::platform::address_map::dram_base,
    });
    for (std::size_t index = 0; index < program.size(); ++index) {
        CHECK(machine.step().status == rv64::StepStatus::Retired);
    }
    const auto state = machine.core().snapshot();
    CHECK(state.registers[3] == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(state.pc == rv64::platform::address_map::dram_base + 16U);
}

void test_boot_layout_and_64_bit_boot_registers()
{
    rv64::platform::Machine machine({
        .ram_size = 8ULL * 1024ULL * 1024ULL,
        .virtual_disk_size = 512ULL,
        .enable_framebuffer = false,
    });
    constexpr std::array<std::uint8_t, 4> firmware{
        0x13U, 0x00U, 0x00U, 0x00U};
    constexpr std::array<std::uint8_t, 4> kernel{
        0x13U, 0x00U, 0x00U, 0x00U};
    constexpr std::array<std::uint8_t, 8> device_tree{
        0xD0U, 0x0DU, 0xFEU, 0xEDU, 0U, 0U, 0U, 0U};
    const auto result = machine.load_boot({
        .firmware = firmware,
        .kernel = kernel,
        .device_tree = device_tree,
        .hart_id = 7U,
    });
    CHECK(result.ok());
    CHECK(
        result.layout.firmware_address ==
        rv64::platform::address_map::dram_base);
    CHECK(
        result.layout.kernel_address ==
        rv64::platform::address_map::dram_base +
            rv64::platform::kernel_load_offset);
    const auto state = machine.core().snapshot();
    CHECK(state.pc == result.layout.firmware_address);
    CHECK(state.registers[10] == 7U);
    CHECK(state.registers[11] == result.layout.device_tree_address);
}

void test_boot_rejects_invalid_layouts()
{
    rv64::platform::Machine machine({
        .ram_size = 1ULL * 1024ULL * 1024ULL,
        .virtual_disk_size = 512ULL,
        .enable_framebuffer = false,
    });
    constexpr std::array<std::uint8_t, 1> byte{0U};
    CHECK(
        machine.load_boot({
            .firmware = {},
            .kernel = byte,
            .device_tree = byte,
        }).error == rv64::platform::BootError::MissingFirmware);
    CHECK(
        machine.load_boot({
            .firmware = byte,
            .kernel = byte,
            .device_tree = byte,
        }).error == rv64::platform::BootError::RamTooSmall);
}

void test_platform_interrupt_wiring()
{
    rv64::platform::Machine machine({
        .ram_size = 1ULL * 1024ULL * 1024ULL,
        .virtual_disk_size = 512ULL,
        .enable_framebuffer = false,
    });
    CHECK(
        machine.bus().write(
            rv64::platform::address_map::clint_base,
            rv::AccessWidth::Word,
            1U,
            rv::AccessKind::Store) == rv::BusFault::None);
    static_cast<void>(machine.step());
    CHECK(machine.irq_lines().machine_software);

    CHECK(
        machine.bus().write(
            rv64::platform::address_map::plic_base +
                4U * rv64::platform::address_map::uart_irq,
            rv::AccessWidth::Word,
            1U,
            rv::AccessKind::Store) == rv::BusFault::None);
    CHECK(
        machine.bus().write(
            rv64::platform::address_map::plic_base + 0x2000U,
            rv::AccessWidth::Word,
            1U << rv64::platform::address_map::uart_irq,
            rv::AccessKind::Store) == rv::BusFault::None);
    CHECK(
        machine.bus().write(
            rv64::platform::address_map::uart_base + 1U,
            rv::AccessWidth::Byte,
            1U,
            rv::AccessKind::Store) == rv::BusFault::None);
    machine.uart().inject_received("x");
    static_cast<void>(machine.step());
    CHECK(machine.irq_lines().machine_external);
}

} // namespace

int main()
{
    test_machine_executes_from_shared_physical_bus();
    test_boot_layout_and_64_bit_boot_registers();
    test_boot_rejects_invalid_layouts();
    test_platform_interrupt_wiring();
    if (failures == 0) {
        std::cout << "All independent RV64 platform tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
