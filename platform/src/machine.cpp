#include "rv32/platform/machine.hpp"

#include <limits>

#include "rv32/devices/clint.hpp"
#include "rv32/devices/framebuffer.hpp"
#include "rv32/devices/plic.hpp"
#include "rv32/devices/ram.hpp"
#include "rv32/devices/syscon.hpp"
#include "rv32/devices/uart16550.hpp"
#include "rv32/devices/virtio_block.hpp"

namespace rv32::platform {

Machine::Machine(const MachineConfig& config)
    : core_(bus_)
{
    ram_ = &bus_.emplace_device<devices::Ram>(
        address_map::dram_base,
        config.ram_size);

    clint_ = &bus_.emplace_device<devices::Clint>(
        address_map::clint_base,
        address_map::clint_size);

    plic_ = &bus_.emplace_device<devices::Plic>(
        address_map::plic_base,
        address_map::plic_size);

    uart_ = &bus_.emplace_device<devices::Uart16550>(
        address_map::uart_base,
        address_map::uart_size);

    virtio_block_ =
        &bus_.emplace_device<devices::VirtioBlock>(
            address_map::virtio_block_base,
            address_map::virtio_block_size,
            config.virtual_disk_size);

    syscon_ = &bus_.emplace_device<devices::Syscon>(
        address_map::syscon_base,
        address_map::syscon_size);

    if (config.enable_framebuffer) {
        framebuffer_ =
            &bus_.emplace_device<devices::Framebuffer>(
                address_map::framebuffer_base,
                config.framebuffer_width,
                config.framebuffer_height,
                config.framebuffer_bytes_per_pixel);
    }

    bus_.set_time_source(clint_);
    reset();
}

Machine::~Machine() = default;

void Machine::reset(const ResetConfig& config)
{
    irq_lines_ = {};
    bus_.reset_performance_counters();
    core_.reset(config);
}

BusFault Machine::load_image(
    std::span<const std::uint8_t> image,
    PhysAddr physical_address) noexcept
{
    if (physical_address < address_map::dram_base) {
        return BusFault::OutOfRange;
    }

    const std::uint64_t offset =
        physical_address - address_map::dram_base;
    return ram_->load_image(image, offset);
}

BootResult Machine::load_boot(const BootConfig& config) noexcept
{
    BootResult result;
    if (config.firmware.empty()) {
        result.error = BootError::MissingFirmware;
        return result;
    }
    if (config.kernel.empty()) {
        result.error = BootError::MissingKernel;
        return result;
    }
    if (config.device_tree.empty()) {
        result.error = BootError::MissingDeviceTree;
        return result;
    }

    const auto ram_range = ram_->range();
    const auto firmware_size =
        static_cast<std::uint64_t>(config.firmware.size());
    const auto kernel_size =
        static_cast<std::uint64_t>(config.kernel.size());
    const auto device_tree_size =
        static_cast<std::uint64_t>(config.device_tree.size());

    if (ram_range.size < kernel_load_offset ||
        device_tree_size > ram_range.size ||
        device_tree_firmware_padding >
            ram_range.size - device_tree_size) {
        result.error = BootError::RamTooSmall;
        return result;
    }
    if (firmware_size > kernel_load_offset) {
        result.error = BootError::ImagesOverlap;
        return result;
    }

    const std::uint64_t device_tree_offset =
        ((ram_range.size -
          device_tree_size -
          device_tree_firmware_padding) /
         device_tree_alignment) *
        device_tree_alignment;
    if (device_tree_offset < kernel_load_offset ||
        kernel_size >
            device_tree_offset - kernel_load_offset) {
        result.error = BootError::ImagesOverlap;
        return result;
    }

    result.layout.device_tree_address =
        ram_range.base + device_tree_offset;
    if (result.layout.firmware_address >
            std::numeric_limits<std::uint32_t>::max() ||
        result.layout.kernel_address >
            std::numeric_limits<std::uint32_t>::max() ||
        result.layout.device_tree_address >
            std::numeric_limits<std::uint32_t>::max()) {
        result.error = BootError::AddressOutOfRange;
        return result;
    }

    if (load_image(
            config.firmware,
            result.layout.firmware_address) != BusFault::None ||
        load_image(
            config.kernel,
            result.layout.kernel_address) != BusFault::None ||
        load_image(
            config.device_tree,
            result.layout.device_tree_address) != BusFault::None) {
        result.error = BootError::ImageLoadFailed;
        return result;
    }

    reset({
        .reset_pc = static_cast<std::uint32_t>(
            result.layout.firmware_address),
        .hart_id = config.hart_id,
        .initial_privilege = PrivilegeMode::Machine,
        .boot_argument = static_cast<std::uint32_t>(
            result.layout.device_tree_address),
    });
    return result;
}

StepResult Machine::step(std::uint64_t elapsed_cycles)
{
    bus_.tick_devices(elapsed_cycles);
    core_.advance_cycles(elapsed_cycles);

    plic_->set_source_level(
        address_map::virtio_block_irq,
        virtio_block_->interrupt_pending());
    plic_->set_source_level(
        address_map::uart_irq,
        uart_->interrupt_pending());

    irq_lines_ = {
        .machine_software = clint_->machine_software_irq(),
        .machine_timer = clint_->machine_timer_irq(),
        .machine_external = plic_->machine_external_irq(),
        .supervisor_software = false,
        .supervisor_timer = false,
        .supervisor_external = plic_->supervisor_external_irq(),
    };

    return core_.step(irq_lines_);
}

Core& Machine::core() noexcept
{
    return core_;
}

const Core& Machine::core() const noexcept
{
    return core_;
}

SystemBus& Machine::bus() noexcept
{
    return bus_;
}

const SystemBus& Machine::bus() const noexcept
{
    return bus_;
}

devices::Ram& Machine::ram() noexcept
{
    return *ram_;
}

devices::Clint& Machine::clint() noexcept
{
    return *clint_;
}

devices::Plic& Machine::plic() noexcept
{
    return *plic_;
}

devices::Uart16550& Machine::uart() noexcept
{
    return *uart_;
}

devices::VirtioBlock& Machine::virtio_block() noexcept
{
    return *virtio_block_;
}

devices::Syscon& Machine::syscon() noexcept
{
    return *syscon_;
}

devices::Framebuffer* Machine::framebuffer() noexcept
{
    return framebuffer_;
}

const IrqLines& Machine::irq_lines() const noexcept
{
    return irq_lines_;
}

std::vector<DeviceInfo> Machine::device_map() const
{
    return bus_.device_map();
}

} // namespace rv32::platform
