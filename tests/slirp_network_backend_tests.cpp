#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "rv/app/slirp_network_backend.hpp"

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

void write_be16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t ipv4_checksum(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::size_t size)
{
    std::uint32_t sum = 0U;
    for (std::size_t index = 0; index < size; index += 2U) {
        sum += static_cast<std::uint32_t>(bytes[offset + index]) << 8U;
        sum += bytes[offset + index + 1U];
    }
    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }
    return static_cast<std::uint16_t>(~sum);
}

[[nodiscard]] std::vector<std::uint8_t> make_dns_query()
{
    constexpr std::size_t ethernet_size = 14U;
    constexpr std::size_t ipv4_size = 20U;
    constexpr std::size_t udp_size = 8U;
    constexpr std::array<std::uint8_t, 29> dns{
        0x12U, 0x34U, 0x01U, 0x00U,
        0x00U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U,
        0x07U, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03U, 'c', 'o', 'm', 0x00U,
        0x00U, 0x01U, 0x00U, 0x01U,
    };
    std::vector<std::uint8_t> frame(
        ethernet_size + ipv4_size + udp_size + dns.size(),
        0U);
    constexpr std::array<std::uint8_t, 6> destination{
        0x52U, 0x55U, 0x0AU, 0x00U, 0x02U, 0x03U};
    constexpr std::array<std::uint8_t, 6> source{
        0x02U, 0x52U, 0x56U, 0x00U, 0x00U, 0x01U};
    std::copy(destination.begin(), destination.end(), frame.begin());
    std::copy(source.begin(), source.end(), frame.begin() + 6U);
    frame[12] = 0x08U;
    frame[13] = 0x00U;

    const std::size_t ip = ethernet_size;
    frame[ip] = 0x45U;
    write_be16(
        frame,
        ip + 2U,
        static_cast<std::uint16_t>(ipv4_size + udp_size + dns.size()));
    write_be16(frame, ip + 4U, 0x1234U);
    write_be16(frame, ip + 6U, 0x4000U);
    frame[ip + 8U] = 64U;
    frame[ip + 9U] = 17U;
    frame[ip + 12U] = 10U;
    frame[ip + 14U] = 2U;
    frame[ip + 15U] = 15U;
    frame[ip + 16U] = 10U;
    frame[ip + 18U] = 2U;
    frame[ip + 19U] = 3U;
    write_be16(
        frame,
        ip + 10U,
        ipv4_checksum(frame, ip, ipv4_size));

    const std::size_t udp = ip + ipv4_size;
    write_be16(frame, udp, 49152U);
    write_be16(frame, udp + 2U, 53U);
    write_be16(
        frame,
        udp + 4U,
        static_cast<std::uint16_t>(udp_size + dns.size()));
    std::copy(
        dns.begin(),
        dns.end(),
        frame.begin() + static_cast<std::ptrdiff_t>(udp + udp_size));
    return frame;
}

[[nodiscard]] std::vector<std::uint8_t> make_arp_reply(
    const std::vector<std::uint8_t>& request)
{
    std::vector<std::uint8_t> reply(42U, 0U);
    std::copy_n(request.begin() + 6U, 6U, reply.begin());
    std::copy_n(request.begin() + 32U, 6U, reply.begin() + 6U);
    reply[12] = 0x08U;
    reply[13] = 0x06U;
    reply[14] = 0x00U;
    reply[15] = 0x01U;
    reply[16] = 0x08U;
    reply[17] = 0x00U;
    reply[18] = 0x06U;
    reply[19] = 0x04U;
    reply[20] = 0x00U;
    reply[21] = 0x02U;
    std::copy_n(request.begin() + 32U, 6U, reply.begin() + 22U);
    std::copy_n(request.begin() + 38U, 4U, reply.begin() + 28U);
    std::copy_n(request.begin() + 22U, 6U, reply.begin() + 32U);
    std::copy_n(request.begin() + 28U, 4U, reply.begin() + 38U);
    return reply;
}

void test_gateway_arp_round_trip()
{
    rv::app::SlirpNetworkBackend backend;
    CHECK(backend.ready());
    CHECK(!backend.version().empty());
    if (!backend.ready()) {
        return;
    }

    constexpr std::array<std::uint8_t, 42> request{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x02, 0x52, 0x56, 0x00, 0x00, 0x01,
        0x08, 0x06,
        0x00, 0x01,
        0x08, 0x00,
        0x06,
        0x04,
        0x00, 0x01,
        0x02, 0x52, 0x56, 0x00, 0x00, 0x01,
        10, 0, 2, 15,
        0, 0, 0, 0, 0, 0,
        10, 0, 2, 2,
    };
    backend.send_frame(request);
    backend.tick(10'000);

    const auto response = backend.receive_frame();
    CHECK(response.has_value());
    if (!response.has_value()) {
        return;
    }
    CHECK(response->size() >= request.size());
    if (response->size() < request.size()) {
        return;
    }

    CHECK((*response)[12] == 0x08U);
    CHECK((*response)[13] == 0x06U);
    CHECK((*response)[20] == 0x00U);
    CHECK((*response)[21] == 0x02U);
    CHECK((*response)[28] == 10U);
    CHECK((*response)[29] == 0U);
    CHECK((*response)[30] == 2U);
    CHECK((*response)[31] == 2U);

    const auto& statistics = backend.statistics();
    CHECK(statistics.guest_to_host_frames == 1U);
    CHECK(statistics.host_to_guest_frames >= 1U);
    CHECK(statistics.guest_errors == 0U);
}

void test_dns_socket_callbacks_are_installed()
{
    rv::app::SlirpNetworkBackend backend;
    CHECK(backend.ready());
    if (!backend.ready()) {
        return;
    }

    const auto query = make_dns_query();
    backend.send_frame(query);
    backend.tick(10'000U);

    const auto& statistics = backend.statistics();
    CHECK(statistics.guest_to_host_frames == 1U);
    CHECK(statistics.poll_socket_registrations >= 1U);
    CHECK(statistics.guest_errors == 0U);
}

void test_live_dns_when_requested()
{
    if (std::getenv("RV_SLIRP_LIVE_DNS") == nullptr) {
        return;
    }

    rv::app::SlirpNetworkBackend backend;
    CHECK(backend.ready());
    if (!backend.ready()) {
        return;
    }

    backend.send_frame(make_dns_query());
    bool received_dns_response = false;
    for (std::size_t attempt = 0U;
         attempt < 500U && !received_dns_response;
         ++attempt) {
        backend.tick(10'000U);
        while (const auto frame = backend.receive_frame()) {
            const bool arp_request_for_guest =
                frame->size() >= 42U &&
                (*frame)[12] == 0x08U &&
                (*frame)[13] == 0x06U &&
                (*frame)[20] == 0x00U &&
                (*frame)[21] == 0x01U &&
                (*frame)[38] == 10U &&
                (*frame)[39] == 0U &&
                (*frame)[40] == 2U &&
                (*frame)[41] == 15U;
            if (arp_request_for_guest) {
                backend.send_frame(make_arp_reply(*frame));
            }
            received_dns_response =
                frame->size() >= 44U &&
                (*frame)[12] == 0x08U &&
                (*frame)[13] == 0x00U &&
                (*frame)[23] == 17U &&
                (*frame)[34] == 0x00U &&
                (*frame)[35] == 0x35U &&
                (*frame)[42] == 0x12U &&
                (*frame)[43] == 0x34U;
        }
        if (!received_dns_response) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    }

    const auto& statistics = backend.statistics();
    CHECK(received_dns_response);
    if (!received_dns_response) {
        std::cerr
            << "Live DNS diagnostics: polls=" << statistics.poll_calls
            << ", observed=" << statistics.poll_socket_observations
            << ", ready=" << statistics.poll_ready_events
            << ", errors=" << statistics.poll_errors
            << ", add=" << statistics.poll_socket_registrations
            << ", remove=" << statistics.poll_socket_unregistrations
            << '\n';
    }
}

} // namespace

int main()
{
    test_gateway_arp_round_trip();
    test_dns_socket_callbacks_are_installed();
    test_live_dns_when_requested();
    if (failures == 0) {
        std::cout << "All libslirp network backend tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
