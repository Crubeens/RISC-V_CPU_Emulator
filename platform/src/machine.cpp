#include "rv32/platform/machine.hpp"

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
    core_.reset(config);
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
