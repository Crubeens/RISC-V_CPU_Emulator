#include "rv32/devices/uart16550.hpp"

#include <utility>

namespace rv32::devices {

namespace {
constexpr std::uint8_t ier_received_data = 1U << 0U;
constexpr std::uint8_t ier_transmitter_empty = 1U << 1U;
constexpr std::uint8_t lcr_divisor_latch = 1U << 7U;
constexpr std::uint8_t lsr_data_ready = 1U << 0U;
constexpr std::uint8_t lsr_transmitter_empty = 1U << 5U;
constexpr std::uint8_t lsr_transmitter_idle = 1U << 6U;
} // namespace

Uart16550::Uart16550(PhysAddr base, std::uint64_t size)
    : range_{.base = base, .size = size}
{
}

std::string_view Uart16550::name() const noexcept
{
    return "NS16550A UART";
}

platform::AddressRange Uart16550::range() const noexcept
{
    return range_;
}

ReadResult Uart16550::read(
    std::uint64_t offset,
    AccessWidth width)
{
    if (width != AccessWidth::Byte || offset >= 8) {
        return {.fault = BusFault::Unsupported};
    }

    std::uint8_t value = 0;
    switch (offset) {
    case 0:
        if (divisor_latch_access()) {
            value = divisor_low_;
        } else if (!receive_fifo_.empty()) {
            value = receive_fifo_.front();
            receive_fifo_.pop_front();
        }
        break;
    case 1:
        value = divisor_latch_access()
                    ? divisor_high_
                    : interrupt_enable_;
        break;
    case 2:
        value = interrupt_identification();
        break;
    case 3:
        value = line_control_;
        break;
    case 4:
        value = modem_control_;
        break;
    case 5:
        value = line_status();
        break;
    case 6:
        value = modem_status_;
        break;
    case 7:
        value = scratch_;
        break;
    default:
        break;
    }

    return {
        .fault = BusFault::None,
        .value = value,
    };
}

BusFault Uart16550::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    if (width != AccessWidth::Byte || offset >= 8) {
        return BusFault::Unsupported;
    }

    const auto byte = static_cast<std::uint8_t>(value);
    switch (offset) {
    case 0:
        if (divisor_latch_access()) {
            divisor_low_ = byte;
        } else {
            transmit_buffer_.push_back(static_cast<char>(byte));
            transmit_interrupt_pending_ = true;
        }
        break;
    case 1:
        if (divisor_latch_access()) {
            divisor_high_ = byte;
        } else {
            const bool transmitter_was_disabled =
                (interrupt_enable_ & ier_transmitter_empty) == 0;
            interrupt_enable_ = byte & 0x0FU;
            if (transmitter_was_disabled &&
                (interrupt_enable_ & ier_transmitter_empty) != 0) {
                transmit_interrupt_pending_ = true;
            }
        }
        break;
    case 2:
        fifo_control_ = byte;
        if ((byte & 0x02U) != 0) {
            receive_fifo_.clear();
        }
        if ((byte & 0x04U) != 0) {
            transmit_buffer_.clear();
        }
        break;
    case 3:
        line_control_ = byte;
        break;
    case 4:
        modem_control_ = byte;
        break;
    case 5:
    case 6:
        break;
    case 7:
        scratch_ = byte;
        break;
    default:
        break;
    }

    return BusFault::None;
}

void Uart16550::inject_received(std::string_view text)
{
    for (const auto character : text) {
        receive_fifo_.push_back(
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(character)));
    }
}

std::string Uart16550::take_transmitted()
{
    auto output = std::move(transmit_buffer_);
    transmit_buffer_.clear();
    return output;
}

bool Uart16550::interrupt_pending() const noexcept
{
    const bool receive_interrupt =
        !receive_fifo_.empty() &&
        (interrupt_enable_ & ier_received_data) != 0;
    const bool transmit_interrupt =
        transmit_interrupt_pending_ &&
        (interrupt_enable_ & ier_transmitter_empty) != 0;
    return receive_interrupt || transmit_interrupt;
}

bool Uart16550::divisor_latch_access() const noexcept
{
    return (line_control_ & lcr_divisor_latch) != 0;
}

std::uint8_t Uart16550::line_status() const noexcept
{
    std::uint8_t status =
        lsr_transmitter_empty | lsr_transmitter_idle;
    if (!receive_fifo_.empty()) {
        status |= lsr_data_ready;
    }
    return status;
}

std::uint8_t Uart16550::interrupt_identification()
{
    if (!receive_fifo_.empty() &&
        (interrupt_enable_ & ier_received_data) != 0) {
        return 0x04U;
    }
    if (transmit_interrupt_pending_ &&
        (interrupt_enable_ & ier_transmitter_empty) != 0) {
        transmit_interrupt_pending_ = false;
        return 0x02U;
    }
    return 0x01U;
}

} // namespace rv32::devices
