#include "rv32/devices/syscon.hpp"

namespace rv32::devices {

Syscon::Syscon(PhysAddr base, std::uint64_t size)
    : range_{.base = base, .size = size}
{
}

std::string_view Syscon::name() const noexcept
{
    return "SYSCON";
}

platform::AddressRange Syscon::range() const noexcept
{
    return range_;
}

ReadResult Syscon::read(
    std::uint64_t offset,
    AccessWidth width)
{
    if (offset != 0 || width != AccessWidth::Word) {
        return {.fault = BusFault::Unsupported};
    }
    return {.fault = BusFault::None, .value = 0};
}

BusFault Syscon::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    if (offset != 0 || width != AccessWidth::Word) {
        return BusFault::Unsupported;
    }

    const auto command = static_cast<std::uint32_t>(value);
    if (command == poweroff_value) {
        action_ = SystemAction::PowerOff;
    } else if (command == reboot_value) {
        action_ = SystemAction::Reboot;
    }

    return BusFault::None;
}

SystemAction Syscon::requested_action() const noexcept
{
    return action_;
}

void Syscon::clear_action() noexcept
{
    action_ = SystemAction::None;
}

} // namespace rv32::devices
