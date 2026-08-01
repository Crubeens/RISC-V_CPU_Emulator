#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <span>
#include <vector>

#include "rv/devices/network_backend.hpp"
#include "rv/devices/ram.hpp"
#include "rv/devices/virtio_net.hpp"
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

constexpr rv::PhysAddr net_base = 0x10002000ULL;
constexpr rv::PhysAddr ram_base = 0x80000000ULL;
constexpr std::uint64_t ram_size = 0x00020000ULL;
constexpr std::uint32_t page_size = 4096;
constexpr std::uint16_t queue_size = 8;
constexpr rv::PhysAddr receive_queue_memory = ram_base;
constexpr rv::PhysAddr transmit_queue_memory = ram_base + 0x2000U;
constexpr rv::PhysAddr receive_buffer = ram_base + 0x10000U;
constexpr rv::PhysAddr transmit_header = ram_base + 0x11000U;
constexpr rv::PhysAddr transmit_buffer = ram_base + 0x11100U;

class TestBackend final : public rv::devices::NetworkBackend {
  public:
    void send_frame(
        std::span<const std::uint8_t> frame) override
    {
        transmitted.emplace_back(frame.begin(), frame.end());
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    receive_frame() override
    {
        if (received.empty()) {
            return std::nullopt;
        }
        auto frame = std::move(received.front());
        received.pop_front();
        return frame;
    }

    std::vector<std::vector<std::uint8_t>> transmitted;
    std::deque<std::vector<std::uint8_t>> received;
};

void write_value(
    rv::platform::SystemBus& bus,
    rv::PhysAddr address,
    rv::AccessWidth width,
    std::uint64_t value)
{
    CHECK(
        bus.write(
            address,
            width,
            value,
            rv::AccessKind::Store) == rv::BusFault::None);
}

[[nodiscard]] std::uint64_t read_value(
    rv::platform::SystemBus& bus,
    rv::PhysAddr address,
    rv::AccessWidth width)
{
    const auto result =
        bus.read(address, width, rv::AccessKind::Load);
    CHECK(result.ok());
    return result.value;
}

[[nodiscard]] constexpr rv::PhysAddr available_ring(
    rv::PhysAddr queue_memory) noexcept
{
    return queue_memory +
           static_cast<std::uint64_t>(queue_size) * 16U;
}

[[nodiscard]] constexpr rv::PhysAddr used_ring(
    rv::PhysAddr queue_memory) noexcept
{
    const auto after_available =
        available_ring(queue_memory) + 4U +
        static_cast<std::uint64_t>(queue_size) * 2U;
    return (after_available + page_size - 1U) &
           ~(static_cast<std::uint64_t>(page_size) - 1U);
}

void configure_queue(
    rv::platform::SystemBus& bus,
    std::uint32_t queue,
    rv::PhysAddr memory)
{
    write_value(
        bus,
        net_base + 0x030U,
        rv::AccessWidth::Word,
        queue);
    write_value(
        bus,
        net_base + 0x038U,
        rv::AccessWidth::Word,
        queue_size);
    write_value(
        bus,
        net_base + 0x03CU,
        rv::AccessWidth::Word,
        page_size);
    write_value(
        bus,
        net_base + 0x040U,
        rv::AccessWidth::Word,
        memory / page_size);
}

void write_descriptor(
    rv::platform::SystemBus& bus,
    rv::PhysAddr queue_memory,
    std::uint16_t index,
    rv::PhysAddr address,
    std::uint32_t length,
    std::uint16_t flags,
    std::uint16_t next)
{
    const auto descriptor =
        queue_memory + static_cast<std::uint64_t>(index) * 16U;
    write_value(
        bus,
        descriptor,
        rv::AccessWidth::DoubleWord,
        address);
    write_value(
        bus,
        descriptor + 8U,
        rv::AccessWidth::Word,
        length);
    write_value(
        bus,
        descriptor + 12U,
        rv::AccessWidth::HalfWord,
        flags);
    write_value(
        bus,
        descriptor + 14U,
        rv::AccessWidth::HalfWord,
        next);
}

void write_bytes(
    rv::platform::SystemBus& bus,
    rv::PhysAddr address,
    std::span<const std::uint8_t> bytes)
{
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        write_value(
            bus,
            address + index,
            rv::AccessWidth::Byte,
            bytes[index]);
    }
}

void test_registers_and_queue_validation()
{
    rv::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv::devices::Ram>(ram_base, ram_size));
    auto& net = bus.emplace_device<rv::devices::VirtioNet>(
        net_base,
        0x1000U);

    CHECK(
        read_value(bus, net_base, rv::AccessWidth::Word) ==
        0x74726976U);
    CHECK(
        read_value(bus, net_base + 0x004U, rv::AccessWidth::Word) ==
        1U);
    CHECK(
        read_value(bus, net_base + 0x008U, rv::AccessWidth::Word) ==
        1U);
    CHECK(
        (read_value(
             bus,
             net_base + 0x010U,
             rv::AccessWidth::Word) &
         ((1U << 5U) | (1U << 16U))) ==
        ((1U << 5U) | (1U << 16U)));

    constexpr std::array<std::uint8_t, 6> expected_mac{
        0x02U, 0x52U, 0x56U, 0x00U, 0x00U, 0x01U};
    for (std::size_t index = 0; index < expected_mac.size(); ++index) {
        CHECK(
            read_value(
                bus,
                net_base + 0x100U + index,
                rv::AccessWidth::Byte) == expected_mac[index]);
    }
    CHECK(
        read_value(
            bus,
            net_base + 0x106U,
            rv::AccessWidth::HalfWord) == 1U);

    write_value(
        bus,
        net_base + 0x028U,
        rv::AccessWidth::Word,
        page_size);
    configure_queue(bus, 0, receive_queue_memory);
    configure_queue(bus, 1, transmit_queue_memory);
    CHECK(net.queue_state(0).configured);
    CHECK(net.queue_state(1).configured);

    write_value(
        bus,
        net_base + 0x030U,
        rv::AccessWidth::Word,
        2U);
    CHECK(
        read_value(
            bus,
            net_base + 0x034U,
            rv::AccessWidth::Word) == 0U);
    write_value(
        bus,
        net_base + 0x050U,
        rv::AccessWidth::Word,
        2U);
    CHECK(net.statistics().rejected_notifications == 1U);

    write_value(
        bus,
        net_base + 0x070U,
        rv::AccessWidth::Word,
        7U);
    CHECK(net.device_status() == 7U);
    write_value(
        bus,
        net_base + 0x070U,
        rv::AccessWidth::Word,
        0U);
    CHECK(net.device_status() == 0U);
    CHECK(!net.queue_state(0).configured);
    CHECK(!net.queue_state(1).configured);
}

void test_transmit_receive_dma_and_interrupts()
{
    rv::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv::devices::Ram>(ram_base, ram_size));
    auto& net = bus.emplace_device<rv::devices::VirtioNet>(
        net_base,
        0x1000U);
    TestBackend backend;
    net.set_backend(&backend);

    write_value(
        bus,
        net_base + 0x028U,
        rv::AccessWidth::Word,
        page_size);
    configure_queue(bus, 0, receive_queue_memory);
    configure_queue(bus, 1, transmit_queue_memory);

    constexpr std::array<std::uint8_t, 14> transmit_frame{
        0x02U, 0x52U, 0x56U, 0x00U, 0x00U, 0x02U,
        0x02U, 0x52U, 0x56U, 0x00U, 0x00U, 0x01U,
        0x08U, 0x06U};
    constexpr std::array<std::uint8_t, 10> header{};
    write_bytes(bus, transmit_header, header);
    write_bytes(bus, transmit_buffer, transmit_frame);
    write_descriptor(
        bus,
        transmit_queue_memory,
        0,
        transmit_header,
        static_cast<std::uint32_t>(header.size()),
        0x1U,
        1U);
    write_descriptor(
        bus,
        transmit_queue_memory,
        1,
        transmit_buffer,
        static_cast<std::uint32_t>(transmit_frame.size()),
        0,
        0);
    write_value(
        bus,
        available_ring(transmit_queue_memory) + 4U,
        rv::AccessWidth::HalfWord,
        0U);
    write_value(
        bus,
        available_ring(transmit_queue_memory) + 2U,
        rv::AccessWidth::HalfWord,
        1U);
    write_value(
        bus,
        net_base + 0x050U,
        rv::AccessWidth::Word,
        rv::devices::VirtioNet::transmit_queue);
    bus.tick_devices(1);

    CHECK(backend.transmitted.size() == 1U);
    CHECK(
        backend.transmitted.front() ==
        std::vector<std::uint8_t>(
            transmit_frame.begin(),
            transmit_frame.end()));
    CHECK(
        read_value(
            bus,
            used_ring(transmit_queue_memory) + 2U,
            rv::AccessWidth::HalfWord) == 1U);
    CHECK(net.interrupt_pending());
    write_value(
        bus,
        net_base + 0x064U,
        rv::AccessWidth::Word,
        1U);
    CHECK(!net.interrupt_pending());

    constexpr std::array<std::uint8_t, 18> receive_frame{
        0x02U, 0x52U, 0x56U, 0x00U, 0x00U, 0x01U,
        0x02U, 0x52U, 0x56U, 0x00U, 0x00U, 0x02U,
        0x08U, 0x00U, 0x45U, 0x00U, 0x00U, 0x14U};
    write_descriptor(
        bus,
        receive_queue_memory,
        0,
        receive_buffer,
        2048U,
        0x2U,
        0);
    write_value(
        bus,
        available_ring(receive_queue_memory) + 4U,
        rv::AccessWidth::HalfWord,
        0U);
    write_value(
        bus,
        available_ring(receive_queue_memory) + 2U,
        rv::AccessWidth::HalfWord,
        1U);
    backend.received.emplace_back(
        receive_frame.begin(),
        receive_frame.end());
    bus.tick_devices(1);

    for (std::size_t index = 0;
         index < rv::devices::VirtioNet::net_header_size;
         ++index) {
        CHECK(
            read_value(
                bus,
                receive_buffer + index,
                rv::AccessWidth::Byte) == 0U);
    }
    for (std::size_t index = 0;
         index < receive_frame.size();
         ++index) {
        CHECK(
            read_value(
                bus,
                receive_buffer +
                    rv::devices::VirtioNet::net_header_size +
                    index,
                rv::AccessWidth::Byte) == receive_frame[index]);
    }
    CHECK(
        read_value(
            bus,
            used_ring(receive_queue_memory) + 8U,
            rv::AccessWidth::Word) ==
        rv::devices::VirtioNet::net_header_size +
            receive_frame.size());
    CHECK(net.interrupt_pending());
    CHECK(net.statistics().transmitted_frames == 1U);
    CHECK(net.statistics().received_frames == 1U);
    CHECK(net.statistics().pending_receive_frames == 0U);
    CHECK(net.statistics().peak_pending_receive_frames == 1U);
    CHECK(net.statistics().receive_queue_starvations == 0U);

    backend.received.emplace_back(
        receive_frame.begin(),
        receive_frame.end());
    bus.tick_devices(1);
    CHECK(net.statistics().pending_receive_frames == 1U);
    CHECK(net.statistics().peak_pending_receive_frames == 1U);
    CHECK(net.statistics().receive_queue_starvations == 1U);
}

void test_short_receive_buffer_and_oversized_frame()
{
    rv::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv::devices::Ram>(ram_base, ram_size));
    auto& net = bus.emplace_device<rv::devices::VirtioNet>(
        net_base,
        0x1000U);

    write_value(
        bus,
        net_base + 0x028U,
        rv::AccessWidth::Word,
        page_size);
    configure_queue(bus, 0, receive_queue_memory);
    write_descriptor(
        bus,
        receive_queue_memory,
        0,
        receive_buffer,
        4U,
        0x2U,
        0);
    write_value(
        bus,
        available_ring(receive_queue_memory) + 4U,
        rv::AccessWidth::HalfWord,
        0U);
    write_value(
        bus,
        available_ring(receive_queue_memory) + 2U,
        rv::AccessWidth::HalfWord,
        1U);

    constexpr std::array<std::uint8_t, 14> frame{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 8, 0};
    CHECK(net.inject_received_frame(frame));
    bus.tick_devices(1);
    CHECK(
        read_value(
            bus,
            used_ring(receive_queue_memory) + 8U,
            rv::AccessWidth::Word) == 0U);
    CHECK(net.statistics().dropped_receive_frames == 1U);

    const std::vector<std::uint8_t> oversized(
        rv::devices::VirtioNet::maximum_frame_size + 1U,
        0U);
    CHECK(!net.inject_received_frame(oversized));
    CHECK(net.statistics().dropped_receive_frames == 2U);
}

} // namespace

int main()
{
    test_registers_and_queue_validation();
    test_transmit_receive_dma_and_interrupts();
    test_short_receive_buffer_and_oversized_frame();
    if (failures == 0) {
        std::cout << "All VirtIO network device tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
