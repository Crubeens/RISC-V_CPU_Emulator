#include "rv64/platform/machine.hpp"

#include "rv32/devices/clint.hpp"
#include "rv32/devices/framebuffer.hpp"
#include "rv32/devices/plic.hpp"
#include "rv32/devices/ram.hpp"
#include "rv32/devices/syscon.hpp"
#include "rv32/devices/uart16550.hpp"
#include "rv32/devices/virtio_block.hpp"

namespace rv64::platform {

Machine::Machine(const MachineConfig& config)
    : core_(bus_)
{
    ram_ = &bus_.emplace_device<rv32::devices::Ram>(
        address_map::dram_base,
        config.ram_size);
    clint_ = &bus_.emplace_device<rv32::devices::Clint>(
        address_map::clint_base,
        address_map::clint_size);
    plic_ = &bus_.emplace_device<rv32::devices::Plic>(
        address_map::plic_base,
        address_map::plic_size);
    uart_ = &bus_.emplace_device<rv32::devices::Uart16550>(
        address_map::uart_base,
        address_map::uart_size);
    virtio_block_ =
        &bus_.emplace_device<rv32::devices::VirtioBlock>(
            address_map::virtio_block_base,
            address_map::virtio_block_size,
            config.virtual_disk_size);
    syscon_ = &bus_.emplace_device<rv32::devices::Syscon>(
        address_map::syscon_base,
        address_map::syscon_size);
    if (config.enable_framebuffer) {
        framebuffer_ =
            &bus_.emplace_device<rv32::devices::Framebuffer>(
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
    bus_.reset_performance_counters();
    bus_.clear_reservations();
    core_.reset(config);
}

rv::BusFault Machine::load_image(
    std::span<const std::uint8_t> image,
    rv::PhysAddr physical_address) noexcept
{
    if (physical_address < address_map::dram_base) {
        return rv::BusFault::OutOfRange;
    }
    return ram_->load_image(
        image,
        physical_address - address_map::dram_base);
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
    const std::uint64_t firmware_size = config.firmware.size();
    const std::uint64_t kernel_size = config.kernel.size();
    const std::uint64_t device_tree_size = config.device_tree.size();
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
        ((ram_range.size - device_tree_size -
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
    if (load_image(
            config.firmware,
            result.layout.firmware_address) != rv::BusFault::None ||
        load_image(
            config.kernel,
            result.layout.kernel_address) != rv::BusFault::None ||
        load_image(
            config.device_tree,
            result.layout.device_tree_address) != rv::BusFault::None) {
        result.error = BootError::ImageLoadFailed;
        return result;
    }

    reset({
        .reset_pc = result.layout.firmware_address,
        .hart_id = config.hart_id,
        .boot_argument = result.layout.device_tree_address,
    });
    return result;
}

StepResult Machine::step(std::uint64_t elapsed_cycles)
{
    bus_.tick_devices(elapsed_cycles);
    return core_.step();
}

Core& Machine::core() noexcept
{
    return core_;
}

const Core& Machine::core() const noexcept
{
    return core_;
}

rv32::platform::SystemBus& Machine::bus() noexcept
{
    return bus_;
}

const rv32::platform::SystemBus& Machine::bus() const noexcept
{
    return bus_;
}

rv32::devices::Ram& Machine::ram() noexcept
{
    return *ram_;
}

} // namespace rv64::platform
