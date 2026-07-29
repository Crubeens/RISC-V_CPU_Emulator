#include <array>
#include <cstdint>
#include <iostream>

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

} // namespace

int main()
{
    test_gateway_arp_round_trip();
    if (failures == 0) {
        std::cout << "All libslirp network backend tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
