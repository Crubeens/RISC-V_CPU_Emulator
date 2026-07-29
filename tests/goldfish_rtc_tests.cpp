#include <cstdint>
#include <iostream>

#include "rv/devices/goldfish_rtc.hpp"
#include "rv/devices/ram.hpp"
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

constexpr rv::PhysAddr rtc_base = 0x00101000ULL;

void test_host_time_set_time_and_alarm()
{
    rv::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv::devices::Ram>(0x80000000ULL, 4096U));
    auto& rtc = bus.emplace_device<rv::devices::GoldfishRtc>(
        rtc_base,
        0x1000U);

    constexpr std::uint64_t requested_time =
        1'700'000'000ULL * 1'000'000'000ULL;
    CHECK(
        bus.write(
            rtc_base + 4U,
            rv::AccessWidth::Word,
            requested_time >> 32U,
            rv::AccessKind::Store) == rv::BusFault::None);
    CHECK(
        bus.write(
            rtc_base,
            rv::AccessWidth::Word,
            static_cast<std::uint32_t>(requested_time),
            rv::AccessKind::Store) == rv::BusFault::None);

    const auto low =
        bus.read(
            rtc_base,
            rv::AccessWidth::Word,
            rv::AccessKind::Load);
    const auto high =
        bus.read(
            rtc_base + 4U,
            rv::AccessWidth::Word,
            rv::AccessKind::Load);
    CHECK(low.ok());
    CHECK(high.ok());
    const auto observed =
        (high.value << 32U) |
        static_cast<std::uint32_t>(low.value);
    CHECK(observed >= requested_time);
    CHECK(observed - requested_time < 1'000'000'000ULL);

    const auto alarm = rtc.time_nanoseconds();
    CHECK(
        bus.write(
            rtc_base + 0x0CU,
            rv::AccessWidth::Word,
            alarm >> 32U,
            rv::AccessKind::Store) == rv::BusFault::None);
    CHECK(
        bus.write(
            rtc_base + 0x08U,
            rv::AccessWidth::Word,
            static_cast<std::uint32_t>(alarm),
            rv::AccessKind::Store) == rv::BusFault::None);
    CHECK(
        bus.write(
            rtc_base + 0x10U,
            rv::AccessWidth::Word,
            1U,
            rv::AccessKind::Store) == rv::BusFault::None);
    bus.tick_devices(1);
    CHECK(rtc.interrupt_pending());
    CHECK(
        bus.write(
            rtc_base + 0x1CU,
            rv::AccessWidth::Word,
            1U,
            rv::AccessKind::Store) == rv::BusFault::None);
    CHECK(!rtc.interrupt_pending());
}

} // namespace

int main()
{
    test_host_time_set_time_and_alarm();
    if (failures == 0) {
        std::cout << "All Goldfish RTC tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
