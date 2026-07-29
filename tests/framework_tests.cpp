#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "rv/devices/clint.hpp"
#include "rv/devices/framebuffer.hpp"
#include "rv/devices/plic.hpp"
#include "rv/devices/ram.hpp"
#include "rv/devices/syscon.hpp"
#include "rv/devices/uart16550.hpp"
#include "rv/devices/virtio_block.hpp"
#include "rv32/platform/address_map.hpp"
#include "rv32/platform/machine.hpp"
#include "rv/platform/system_bus.hpp"

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

using rv32::AccessKind;
using rv32::AccessWidth;
using rv32::AmoOperation;
using rv32::BusFault;
using rv32::PhysAddr;
using rv::platform::SystemBus;

void test_bus_ram_and_atomics()
{
    constexpr PhysAddr ram_base = 0x80000000ULL;
    SystemBus bus;
    auto& ram =
        bus.emplace_device<rv::devices::Ram>(ram_base, 0x1000);

    CHECK(
        bus.write(
            ram_base + 4U,
            AccessWidth::Word,
            0x12345678U,
            AccessKind::Store) == BusFault::None);

    const auto word =
        bus.read(ram_base + 4U, AccessWidth::Word, AccessKind::Load);
    CHECK(word.ok());
    CHECK(word.value == 0x12345678U);

    const auto byte =
        bus.read(ram_base + 5U, AccessWidth::Byte, AccessKind::Load);
    CHECK(byte.ok());
    CHECK(byte.value == 0x56U);

    CHECK(
        bus.read(
               ram_base + 0x1000U,
               AccessWidth::Byte,
               AccessKind::Load)
            .fault == BusFault::Unmapped);
    CHECK(
        bus.read(
               ram_base + 0x0FFFU,
               AccessWidth::HalfWord,
               AccessKind::Load)
            .fault == BusFault::Unmapped);
    CHECK(
        ram.read(0x0FFFU, AccessWidth::HalfWord).fault ==
        BusFault::OutOfRange);
    const std::vector<std::uint8_t> oversized_image(2U, 0xA5U);
    CHECK(
        ram.load_image(oversized_image, 0x0FFFU) ==
        BusFault::OutOfRange);

    CHECK(ram.bytes().size() == 0x1000U);

    bool overlap_rejected = false;
    try {
        static_cast<void>(
            bus.emplace_device<rv::devices::Ram>(
                ram_base + 0x800U,
                0x100U));
    } catch (const std::invalid_argument&) {
        overlap_rejected = true;
    }
    CHECK(overlap_rejected);

    CHECK(
        bus.write(
            ram_base + 8U,
            AccessWidth::Word,
            10U,
            AccessKind::Store) == BusFault::None);

    const auto reserved =
        bus.load_reserved_word(0, ram_base + 8U);
    CHECK(reserved.ok());
    CHECK(reserved.value == 10U);

    CHECK(
        bus.dma_write(
            ram_base + 12U,
            AccessWidth::Word,
            99U) == BusFault::None);

    const auto failed_sc =
        bus.store_conditional_word(0, ram_base + 8U, 20U);
    CHECK(failed_sc.ok());
    CHECK(!failed_sc.succeeded);

    CHECK(bus.load_reserved_word(0, ram_base + 8U).ok());
    const auto successful_sc =
        bus.store_conditional_word(0, ram_base + 8U, 30U);
    CHECK(successful_sc.ok());
    CHECK(successful_sc.succeeded);

    const auto amo = bus.atomic_word(
        0,
        ram_base + 8U,
        AmoOperation::Add,
        2U);
    CHECK(amo.ok());
    CHECK(amo.original_value == 30U);
    CHECK(
        bus.read(
               ram_base + 8U,
               AccessWidth::Word,
               AccessKind::Load)
            .value == 32U);
}

void test_clint()
{
    constexpr PhysAddr base = 0x02000000ULL;
    SystemBus bus;
    auto& clint =
        bus.emplace_device<rv::devices::Clint>(base, 0x10000);
    bus.set_time_source(&clint);

    CHECK(
        bus.write(
            base + 0x4000U,
            AccessWidth::Word,
            5U,
            AccessKind::Store) == BusFault::None);
    CHECK(
        bus.write(
            base + 0x4004U,
            AccessWidth::Word,
            0U,
            AccessKind::Store) == BusFault::None);

    bus.tick_devices(4);
    CHECK(bus.read_time() == 4U);
    CHECK(!clint.machine_timer_irq());

    bus.tick_devices(1);
    CHECK(bus.read_time() == 5U);
    CHECK(clint.machine_timer_irq());

    CHECK(
        bus.write(
            base,
            AccessWidth::Word,
            1U,
            AccessKind::Store) == BusFault::None);
    CHECK(clint.machine_software_irq());
}

void test_plic()
{
    constexpr PhysAddr base = 0x0C000000ULL;
    SystemBus bus;
    auto& plic =
        bus.emplace_device<rv::devices::Plic>(
            base,
            0x04000000ULL);

    CHECK(
        bus.write(
            base + 4U,
            AccessWidth::Word,
            3U,
            AccessKind::Store) == BusFault::None);

    constexpr auto supervisor_enable = 0x2000U + 0x80U;
    CHECK(
        bus.write(
            base + supervisor_enable,
            AccessWidth::Word,
            1U << 1U,
            AccessKind::Store) == BusFault::None);

    constexpr auto supervisor_context = 0x200000U + 0x1000U;
    CHECK(
        bus.write(
            base + supervisor_context,
            AccessWidth::Word,
            0U,
            AccessKind::Store) == BusFault::None);

    plic.set_source_level(1, true);
    CHECK(plic.supervisor_external_irq());

    const auto claim = bus.read(
        base + supervisor_context + 4U,
        AccessWidth::Word,
        AccessKind::Load);
    CHECK(claim.ok());
    CHECK(claim.value == 1U);
    CHECK(!plic.supervisor_external_irq());

    CHECK(
        bus.write(
            base + supervisor_context + 4U,
            AccessWidth::Word,
            1U,
            AccessKind::Store) == BusFault::None);
    CHECK(plic.supervisor_external_irq());

    plic.set_source_level(1, false);
    CHECK(
        bus.read(
               base + supervisor_context + 4U,
               AccessWidth::Word,
               AccessKind::Load)
            .value == 1U);
    CHECK(
        bus.write(
            base + supervisor_context + 4U,
            AccessWidth::Word,
            1U,
            AccessKind::Store) == BusFault::None);
    CHECK(!plic.supervisor_external_irq());
}

void test_uart_syscon_and_framebuffer()
{
    constexpr PhysAddr uart_base = 0x10000000ULL;
    constexpr PhysAddr syscon_base = 0x11100000ULL;
    constexpr PhysAddr framebuffer_base = 0x40000000ULL;

    SystemBus bus;
    auto& uart =
        bus.emplace_device<rv::devices::Uart16550>(
            uart_base,
            0x100);
    auto& syscon =
        bus.emplace_device<rv::devices::Syscon>(
            syscon_base,
            0x1000);
    auto& framebuffer =
        bus.emplace_device<rv::devices::Framebuffer>(
            framebuffer_base,
            16,
            8,
            4);

    CHECK(
        bus.write(
            uart_base + 1U,
            AccessWidth::Byte,
            1U,
            AccessKind::Store) == BusFault::None);
    uart.inject_received("A");
    CHECK(uart.interrupt_pending());
    CHECK(
        bus.read(
               uart_base,
               AccessWidth::Byte,
               AccessKind::Load)
            .value == static_cast<std::uint8_t>('A'));
    CHECK(!uart.interrupt_pending());

    CHECK(
        bus.write(
            uart_base,
            AccessWidth::Byte,
            static_cast<std::uint8_t>('Z'),
            AccessKind::Store) == BusFault::None);
    CHECK(uart.take_transmitted() == "Z");

    CHECK(
        bus.write(
            syscon_base,
            AccessWidth::Word,
            rv::devices::Syscon::poweroff_value,
            AccessKind::Store) == BusFault::None);
    CHECK(
        syscon.requested_action() ==
        rv::devices::SystemAction::PowerOff);
    syscon.clear_action();
    CHECK(
        syscon.requested_action() ==
        rv::devices::SystemAction::None);

    CHECK(
        bus.write(
            framebuffer_base,
            AccessWidth::Word,
            0xAABBCCDDU,
            AccessKind::Store) == BusFault::None);
    CHECK(framebuffer.dirty());
    CHECK(framebuffer.dirty_region().x == 0U);
    CHECK(framebuffer.dirty_region().y == 0U);
    CHECK(framebuffer.dirty_region().width == 1U);
    CHECK(framebuffer.dirty_region().height == 1U);
    CHECK(framebuffer.statistics().write_operations == 1U);
    CHECK(framebuffer.statistics().bytes_written == 4U);
    CHECK(
        bus.read(
               framebuffer_base,
               AccessWidth::Word,
               AccessKind::Load)
            .value == 0xAABBCCDDU);
    framebuffer.clear_dirty();
    CHECK(!framebuffer.dirty());
    CHECK(framebuffer.dirty_region().empty());

    CHECK(
        bus.write(
            framebuffer_base + (2U * 16U + 5U) * 4U,
            AccessWidth::Word,
            0x01020304U,
            AccessKind::Store) == BusFault::None);
    CHECK(framebuffer.dirty_region().x == 5U);
    CHECK(framebuffer.dirty_region().y == 2U);
    CHECK(framebuffer.dirty_region().width == 1U);
    CHECK(framebuffer.dirty_region().height == 1U);
    CHECK(framebuffer.statistics().write_operations == 2U);
    CHECK(framebuffer.statistics().bytes_written == 8U);
}

void test_framebuffer_rejects_overflowing_dimensions()
{
    bool rejected = false;
    try {
        rv::devices::Framebuffer framebuffer(
            0x40000000ULL,
            std::numeric_limits<std::uint32_t>::max(),
            std::numeric_limits<std::uint32_t>::max(),
            std::numeric_limits<std::uint32_t>::max());
        static_cast<void>(framebuffer);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

void test_virtio_block_read()
{
    constexpr PhysAddr ram_base = 0x80000000ULL;
    constexpr PhysAddr virtio_base = 0x10001000ULL;
    constexpr PhysAddr descriptor_table = ram_base + 0x1000U;
    constexpr PhysAddr available_ring = descriptor_table + 8U * 16U;
    constexpr PhysAddr used_ring = ram_base + 0x2000U;
    constexpr PhysAddr request_header = ram_base + 0x3000U;
    constexpr PhysAddr data_buffer = ram_base + 0x3100U;
    constexpr PhysAddr status_byte = ram_base + 0x3400U;

    std::vector<std::uint8_t> disk(512);
    disk.front() = 0xA5U;
    disk.back() = 0x5AU;

    SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv::devices::Ram>(
            ram_base,
            0x10000));
    auto& virtio =
        bus.emplace_device<rv::devices::VirtioBlock>(
            virtio_base,
            0x1000,
            std::move(disk));

    const auto store = [&bus](
                           PhysAddr address,
                           AccessWidth width,
                           std::uint64_t value) {
        return bus.write(address, width, value, AccessKind::Store);
    };

    CHECK(
        store(
            descriptor_table,
            AccessWidth::DoubleWord,
            request_header) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 8U,
            AccessWidth::Word,
            16U) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 12U,
            AccessWidth::HalfWord,
            1U) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 14U,
            AccessWidth::HalfWord,
            1U) == BusFault::None);

    CHECK(
        store(
            descriptor_table + 16U,
            AccessWidth::DoubleWord,
            data_buffer) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 24U,
            AccessWidth::Word,
            512U) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 28U,
            AccessWidth::HalfWord,
            0x3U) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 30U,
            AccessWidth::HalfWord,
            2U) == BusFault::None);

    CHECK(
        store(
            descriptor_table + 32U,
            AccessWidth::DoubleWord,
            status_byte) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 40U,
            AccessWidth::Word,
            1U) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 44U,
            AccessWidth::HalfWord,
            0x2U) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 46U,
            AccessWidth::HalfWord,
            0U) == BusFault::None);

    CHECK(
        store(
            request_header,
            AccessWidth::Word,
            0U) == BusFault::None);
    CHECK(
        store(
            request_header + 8U,
            AccessWidth::DoubleWord,
            0U) == BusFault::None);
    CHECK(
        store(
            status_byte,
            AccessWidth::Byte,
            0xFFU) == BusFault::None);

    CHECK(
        store(
            available_ring,
            AccessWidth::HalfWord,
            0U) == BusFault::None);
    CHECK(
        store(
            available_ring + 2U,
            AccessWidth::HalfWord,
            1U) == BusFault::None);
    CHECK(
        store(
            available_ring + 4U,
            AccessWidth::HalfWord,
            0U) == BusFault::None);

    CHECK(
        store(
            virtio_base + 0x28U,
            AccessWidth::Word,
            4096U) == BusFault::None);
    CHECK(
        store(
            virtio_base + 0x70U,
            AccessWidth::Word,
            0U) == BusFault::None);
    CHECK(virtio.queue_state().page_size == 4096U);
    CHECK(
        store(
            virtio_base + 0x38U,
            AccessWidth::Word,
            8U) == BusFault::None);
    CHECK(
        store(
            virtio_base + 0x3CU,
            AccessWidth::Word,
            4096U) == BusFault::None);
    CHECK(
        store(
            virtio_base + 0x40U,
            AccessWidth::Word,
            descriptor_table / 4096U) == BusFault::None);
    CHECK(
        store(
            virtio_base + 0x70U,
            AccessWidth::Word,
            4U) == BusFault::None);
    CHECK(
        store(
            virtio_base + 0x50U,
            AccessWidth::Word,
            0U) == BusFault::None);

    bus.tick_devices(1);

    CHECK(
        bus.read(
               data_buffer,
               AccessWidth::Byte,
               AccessKind::Load)
            .value == 0xA5U);
    CHECK(
        bus.read(
               data_buffer + 511U,
               AccessWidth::Byte,
               AccessKind::Load)
            .value == 0x5AU);
    CHECK(
        bus.read(
               status_byte,
               AccessWidth::Byte,
               AccessKind::Load)
            .value == 0U);
    CHECK(
        bus.read(
               used_ring + 2U,
               AccessWidth::HalfWord,
               AccessKind::Load)
            .value == 1U);
    CHECK(virtio.interrupt_pending());
    CHECK(
        bus.read(
               virtio_base + 0x60U,
               AccessWidth::Word,
               AccessKind::Load)
            .value == 1U);

    CHECK(
        store(
            virtio_base + 0x64U,
            AccessWidth::Word,
            1U) == BusFault::None);
    CHECK(!virtio.interrupt_pending());

    CHECK(
        store(
            request_header,
            AccessWidth::Word,
            1U) == BusFault::None);
    CHECK(
        store(
            descriptor_table + 28U,
            AccessWidth::HalfWord,
            0x1U) == BusFault::None);
    CHECK(
        store(
            data_buffer,
            AccessWidth::Byte,
            0x11U) == BusFault::None);
    CHECK(
        store(
            data_buffer + 511U,
            AccessWidth::Byte,
            0x22U) == BusFault::None);
    CHECK(
        store(
            status_byte,
            AccessWidth::Byte,
            0xFFU) == BusFault::None);
    CHECK(
        store(
            available_ring + 6U,
            AccessWidth::HalfWord,
            0U) == BusFault::None);
    CHECK(
        store(
            available_ring + 2U,
            AccessWidth::HalfWord,
            2U) == BusFault::None);
    CHECK(
        store(
            virtio_base + 0x50U,
            AccessWidth::Word,
            0U) == BusFault::None);

    bus.tick_devices(1);

    CHECK(virtio.disk_image().front() == 0x11U);
    CHECK(virtio.disk_image().back() == 0x22U);
    CHECK(virtio.dirty());
    CHECK(
        bus.read(
               status_byte,
               AccessWidth::Byte,
               AccessKind::Load)
            .value == 0U);
    CHECK(
        bus.read(
               used_ring + 2U,
               AccessWidth::HalfWord,
               AccessKind::Load)
            .value == 2U);
    CHECK(virtio.interrupt_pending());
    CHECK(
        store(
            virtio_base + 0x64U,
            AccessWidth::Word,
            1U) == BusFault::None);
    CHECK(!virtio.interrupt_pending());
}

void test_machine_framework()
{
    rv32::platform::MachineConfig config;
    config.ram_size = 1024U * 1024U;
    config.virtual_disk_size = 512U;
    config.framebuffer_width = 64;
    config.framebuffer_height = 32;

    rv32::platform::Machine machine(config);
    const auto snapshot = machine.core().snapshot();

    CHECK(
        snapshot.pc ==
        rv32::platform::address_map::default_reset_pc);
    CHECK(snapshot.registers[0] == 0U);
    CHECK(
        rv32::Core::isa_string() ==
        "rv32imac_zicntr_zicsr_zifencei");
    CHECK(machine.device_map().size() == 7U);

    const auto result = machine.step(3);
    CHECK(
        result.pc ==
        rv32::platform::address_map::default_reset_pc);
    CHECK(result.cycle == 3U);
    CHECK(machine.core().snapshot().cycle == 3U);

    const auto magic = machine.bus().read(
        rv32::platform::address_map::virtio_block_base,
        AccessWidth::Word,
        AccessKind::Load);
    CHECK(magic.ok());
    CHECK(magic.value == 0x74726976U);
}

void test_machine_executes_load_and_store_through_ram()
{
    rv32::platform::MachineConfig config;
    config.ram_size = 0x1000U;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;

    rv32::platform::Machine machine(config);
    constexpr PhysAddr base =
        rv32::platform::address_map::dram_base;

    CHECK(
        machine.bus().dma_write(
            base,
            AccessWidth::Word,
            0x800000B7U) == BusFault::None); // lui x1, 0x80000
    CHECK(
        machine.bus().dma_write(
            base + 4U,
            AccessWidth::Word,
            0x1000A103U) == BusFault::None); // lw x2, 0x100(x1)
    CHECK(
        machine.bus().dma_write(
            base + 8U,
            AccessWidth::Word,
            0x1020A223U) == BusFault::None); // sw x2, 0x104(x1)
    CHECK(
        machine.bus().dma_write(
            base + 0x100U,
            AccessWidth::Word,
            0xDEADBEEFU) == BusFault::None);

    CHECK(machine.step().status == rv32::StepStatus::Retired);
    CHECK(machine.step().status == rv32::StepStatus::Retired);
    CHECK(machine.step().status == rv32::StepStatus::Retired);

    const auto stored = machine.bus().dma_read(
        base + 0x104U,
        AccessWidth::Word);
    CHECK(stored.ok());
    CHECK(stored.value == 0xDEADBEEFU);

    const auto state = machine.core().snapshot();
    CHECK(state.registers[1] == 0x80000000U);
    CHECK(state.registers[2] == 0xDEADBEEFU);
    CHECK(state.pc == static_cast<std::uint32_t>(base + 12U));
    CHECK(state.instructions_retired == 3U);
    CHECK(state.cycle == 3U);
}

void test_machine_loads_images_by_physical_address()
{
    constexpr std::uint64_t ram_size =
        8ULL * 1024ULL * 1024ULL;
    constexpr std::size_t kernel_offset =
        4U * 1024U * 1024U;
    constexpr std::array<std::uint8_t, 4> image{
        0x13U,
        0x00U,
        0x00U,
        0x00U,
    };

    rv32::platform::MachineConfig config;
    config.ram_size = ram_size;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;
    rv32::platform::Machine machine(config);

    const PhysAddr kernel_address =
        rv32::platform::address_map::dram_base +
        kernel_offset;
    CHECK(
        machine.load_image(image, kernel_address) ==
        BusFault::None);

    const auto bytes = machine.ram().bytes();
    CHECK(bytes[kernel_offset + 0U] == image[0]);
    CHECK(bytes[kernel_offset + 1U] == image[1]);
    CHECK(bytes[kernel_offset + 2U] == image[2]);
    CHECK(bytes[kernel_offset + 3U] == image[3]);

    CHECK(
        machine.load_image(
            image,
            rv32::platform::address_map::dram_base - 1U) ==
        BusFault::OutOfRange);
    CHECK(
        machine.load_image(
            image,
            rv32::platform::address_map::dram_base +
                ram_size - 2U) ==
        BusFault::OutOfRange);

    constexpr std::uint32_t dtb_address = 0x807FF000U;
    machine.reset({
        .reset_pc =
            rv32::platform::address_map::default_reset_pc,
        .hart_id = 3U,
        .initial_privilege = rv32::PrivilegeMode::Machine,
        .boot_argument = dtb_address,
    });
    const auto state = machine.core().snapshot();
    CHECK(state.registers[10] == state.hart_id);
    CHECK(state.registers[10] == 3U);
    CHECK(state.registers[11] == dtb_address);
}

void test_machine_prepares_fixed_linux_boot_layout()
{
    constexpr std::uint64_t ram_size =
        8ULL * 1024ULL * 1024ULL;
    constexpr std::array<std::uint8_t, 4> firmware{
        0x13U,
        0x00U,
        0x00U,
        0x00U,
    };
    constexpr std::array<std::uint8_t, 3> kernel{
        0xAAU,
        0xBBU,
        0xCCU,
    };
    constexpr std::array<std::uint8_t, 5> device_tree{
        0xD0U,
        0x0DU,
        0xFEU,
        0xEDU,
        0x01U,
    };
    constexpr std::uint64_t expected_dtb_offset =
        ((ram_size -
          device_tree.size() -
          rv32::platform::device_tree_firmware_padding) /
         rv32::platform::device_tree_alignment) *
        rv32::platform::device_tree_alignment;

    rv32::platform::MachineConfig machine_config;
    machine_config.ram_size = ram_size;
    machine_config.virtual_disk_size = 512U;
    machine_config.enable_framebuffer = false;
    rv32::platform::Machine machine(machine_config);

    const auto result = machine.load_boot({
        .firmware = firmware,
        .kernel = kernel,
        .device_tree = device_tree,
        .hart_id = 5U,
    });
    CHECK(result.ok());
    CHECK(result.error == rv32::platform::BootError::None);
    CHECK(
        result.layout.firmware_address ==
        rv32::platform::address_map::dram_base);
    CHECK(
        result.layout.kernel_address ==
        rv32::platform::address_map::dram_base +
            rv32::platform::kernel_load_offset);
    CHECK(
        result.layout.device_tree_address ==
        rv32::platform::address_map::dram_base +
            expected_dtb_offset);

    const auto bytes = machine.ram().bytes();
    CHECK(bytes[0] == firmware[0]);
    CHECK(bytes[3] == firmware[3]);
    CHECK(
        bytes[rv32::platform::kernel_load_offset] ==
        kernel[0]);
    CHECK(
        bytes[rv32::platform::kernel_load_offset + 2U] ==
        kernel[2]);
    CHECK(bytes[expected_dtb_offset] == device_tree[0]);
    CHECK(
        bytes[expected_dtb_offset + 4U] ==
        device_tree[4]);
    CHECK(
        ram_size -
            (expected_dtb_offset + device_tree.size()) >=
        rv32::platform::device_tree_firmware_padding);

    const auto state = machine.core().snapshot();
    CHECK(
        state.pc ==
        rv32::platform::address_map::default_reset_pc);
    CHECK(state.hart_id == 5U);
    CHECK(state.registers[10] == 5U);
    CHECK(
        state.registers[11] ==
        static_cast<std::uint32_t>(
            result.layout.device_tree_address));
}

void test_machine_rejects_invalid_boot_layouts_before_loading()
{
    constexpr std::array<std::uint8_t, 1> image{0xA5U};
    constexpr std::array<std::uint8_t, 4096> page{};

    rv32::platform::MachineConfig config;
    config.ram_size =
        rv32::platform::kernel_load_offset +
        rv32::platform::device_tree_alignment;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;
    rv32::platform::Machine machine(config);

    CHECK(
        machine.load_boot({
            .firmware = {},
            .kernel = image,
            .device_tree = image,
        }).error ==
        rv32::platform::BootError::MissingFirmware);
    CHECK(
        machine.load_boot({
            .firmware = image,
            .kernel = {},
            .device_tree = image,
        }).error ==
        rv32::platform::BootError::MissingKernel);
    CHECK(
        machine.load_boot({
            .firmware = image,
            .kernel = image,
            .device_tree = {},
        }).error ==
        rv32::platform::BootError::MissingDeviceTree);

    const auto overlap = machine.load_boot({
        .firmware = image,
        .kernel = page,
        .device_tree = image,
    });
    CHECK(
        overlap.error ==
        rv32::platform::BootError::ImagesOverlap);
    CHECK(machine.ram().bytes()[0] == 0U);

    rv32::platform::MachineConfig small_config;
    small_config.ram_size =
        rv32::platform::kernel_load_offset - 1U;
    small_config.virtual_disk_size = 512U;
    small_config.enable_framebuffer = false;
    rv32::platform::Machine small_machine(small_config);
    CHECK(
        small_machine.load_boot({
            .firmware = image,
            .kernel = image,
            .device_tree = image,
        }).error ==
        rv32::platform::BootError::RamTooSmall);
}

} // namespace

int main()
{
    test_bus_ram_and_atomics();
    test_clint();
    test_plic();
    test_uart_syscon_and_framebuffer();
    test_framebuffer_rejects_overflowing_dimensions();
    test_virtio_block_read();
    test_machine_framework();
    test_machine_executes_load_and_store_through_ram();
    test_machine_loads_images_by_physical_address();
    test_machine_prepares_fixed_linux_boot_layout();
    test_machine_rejects_invalid_boot_layouts_before_loading();

    if (failures == 0) {
        std::cout << "All RV32 framework tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
