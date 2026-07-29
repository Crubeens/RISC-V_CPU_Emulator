#include "rv/devices/goldfish_rtc.hpp"

#include <chrono>
#include <limits>

namespace rv::devices {

namespace {

[[nodiscard]] std::int64_t host_time_nanoseconds() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

GoldfishRtc::GoldfishRtc(PhysAddr base, std::uint64_t size)
    : range_{.base = base, .size = size}
{
}

std::string_view GoldfishRtc::name() const noexcept
{
    return "Goldfish RTC";
}

platform::AddressRange GoldfishRtc::range() const noexcept
{
    return range_;
}

ReadResult GoldfishRtc::read(
    std::uint64_t offset,
    AccessWidth width)
{
    if (width != AccessWidth::Word || (offset & 0x3U) != 0) {
        return {.fault = BusFault::Unsupported};
    }

    std::uint32_t value = 0;
    switch (offset) {
    case time_low_offset: {
        const auto now = time_nanoseconds();
        latched_time_high_ = static_cast<std::uint32_t>(now >> 32U);
        value = static_cast<std::uint32_t>(now);
        break;
    }
    case time_high_offset:
        value = latched_time_high_;
        break;
    case alarm_low_offset:
        value = static_cast<std::uint32_t>(alarm_time_ns_);
        break;
    case alarm_high_offset:
        value = static_cast<std::uint32_t>(alarm_time_ns_ >> 32U);
        break;
    case irq_enabled_offset:
        value = irq_enabled_ ? 1U : 0U;
        break;
    case alarm_status_offset:
        value = alarm_active_ ? 1U : 0U;
        break;
    default:
        break;
    }
    return {.fault = BusFault::None, .value = value};
}

BusFault GoldfishRtc::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    if (width != AccessWidth::Word || (offset & 0x3U) != 0) {
        return BusFault::Unsupported;
    }

    const auto word = static_cast<std::uint32_t>(value);
    switch (offset) {
    case time_high_offset:
        pending_time_high_ = word;
        break;
    case time_low_offset:
        set_time(
            (static_cast<std::uint64_t>(pending_time_high_) << 32U) |
            word);
        break;
    case alarm_high_offset:
        pending_alarm_high_ = word;
        break;
    case alarm_low_offset:
        alarm_time_ns_ =
            (static_cast<std::uint64_t>(pending_alarm_high_) << 32U) |
            word;
        alarm_active_ = true;
        interrupt_pending_ = false;
        break;
    case irq_enabled_offset:
        irq_enabled_ = word != 0;
        if (!irq_enabled_) {
            interrupt_pending_ = false;
        }
        break;
    case clear_alarm_offset:
        if (word != 0) {
            alarm_active_ = false;
            interrupt_pending_ = false;
        }
        break;
    case clear_interrupt_offset:
        if (word != 0) {
            interrupt_pending_ = false;
        }
        break;
    default:
        break;
    }
    return BusFault::None;
}

void GoldfishRtc::tick(
    platform::DmaAccess& dma,
    std::uint64_t cycles)
{
    static_cast<void>(dma);
    static_cast<void>(cycles);
    if (alarm_active_ && time_nanoseconds() >= alarm_time_ns_) {
        alarm_active_ = false;
        interrupt_pending_ = irq_enabled_;
    }
}

bool GoldfishRtc::interrupt_pending() const noexcept
{
    return interrupt_pending_;
}

std::uint64_t GoldfishRtc::time_nanoseconds() const noexcept
{
    const auto host = host_time_nanoseconds();
    if (host_time_offset_ns_ >= 0) {
        const auto offset =
            static_cast<std::uint64_t>(host_time_offset_ns_);
        const auto current = static_cast<std::uint64_t>(host);
        if (current >
            std::numeric_limits<std::uint64_t>::max() - offset) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return current + offset;
    }

    const auto magnitude =
        static_cast<std::uint64_t>(
            -(host_time_offset_ns_ + 1)) +
        1U;
    const auto current = static_cast<std::uint64_t>(host);
    return current < magnitude ? 0U : current - magnitude;
}

void GoldfishRtc::set_time(std::uint64_t nanoseconds) noexcept
{
    const auto host = host_time_nanoseconds();
    const auto maximum =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());
    if (nanoseconds > maximum) {
        host_time_offset_ns_ =
            std::numeric_limits<std::int64_t>::max();
        return;
    }

    const auto desired = static_cast<std::int64_t>(nanoseconds);
    host_time_offset_ns_ =
        desired >= host
            ? desired - host
            : -(host - desired);
}

} // namespace rv::devices
