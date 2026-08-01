#include "rv/app/slirp_network_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <slirp/libslirp.h>

#include "rv/devices/virtio_net.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <poll.h>
#endif

namespace rv::app {

namespace {

constexpr std::uint64_t poll_interval_cycles = 10'000;
constexpr std::size_t maximum_queued_frames = 256;

[[nodiscard]] std::int64_t monotonic_nanoseconds() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] bool parse_ipv4(
    const char* text,
    in_addr& address) noexcept
{
#if defined(_WIN32)
    return slirp_inet_aton(text, &address) != 0;
#else
    return inet_pton(AF_INET, text, &address) == 1;
#endif
}

} // namespace

struct SlirpNetworkBackend::Impl {
    struct Timer {
        SlirpTimerCb callback{};
        void* callback_opaque{};
        std::int64_t expires_ms{
            std::numeric_limits<std::int64_t>::max()};
        bool enabled{};
    };

    struct PollEntry {
        slirp_os_socket socket{SLIRP_INVALID_SOCKET};
#if defined(_WIN32)
        WSAPOLLFD descriptor{};
#else
        pollfd descriptor{};
#endif
    };

    struct PollContext {
        std::vector<PollEntry> entries;
    };

    Impl()
    {
#if defined(_WIN32)
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "WSAStartup failed";
            return;
        }
        winsock_started = true;
#endif

        config = {};
        config.version = SLIRP_CONFIG_VERSION_MAX;
        config.restricted = 0;
        config.in_enabled = true;
        config.in6_enabled = false;
        config.vhostname = "rv64-emulator";
        config.if_mtu = 1500;
        config.if_mru = 1500;
        config.disable_host_loopback = true;
        config.disable_dns = false;
        config.disable_dhcp = false;
        if (!parse_ipv4("10.0.2.0", config.vnetwork) ||
            !parse_ipv4("255.255.255.0", config.vnetmask) ||
            !parse_ipv4("10.0.2.2", config.vhost) ||
            !parse_ipv4("10.0.2.15", config.vdhcp_start) ||
            !parse_ipv4("10.0.2.3", config.vnameserver)) {
            error = "cannot configure the libslirp IPv4 network";
            return;
        }

        callbacks = {};
        callbacks.send_packet = &send_packet_callback;
        callbacks.guest_error = &guest_error_callback;
        callbacks.clock_get_ns = &clock_get_ns_callback;
        callbacks.timer_new = &timer_new_callback;
        callbacks.timer_free = &timer_free_callback;
        callbacks.timer_mod = &timer_mod_callback;
        callbacks.notify = &notify_callback;
        callbacks.register_poll_socket =
            &register_poll_socket_callback;
        callbacks.unregister_poll_socket =
            &unregister_poll_socket_callback;

        slirp = slirp_new(&config, &callbacks, this);
        if (slirp == nullptr) {
            error = "slirp_new failed";
        }
    }

    ~Impl()
    {
        if (slirp != nullptr) {
            slirp_cleanup(slirp);
            slirp = nullptr;
        }
        for (auto* timer : timers) {
            delete timer;
        }
        timers.clear();
#if defined(_WIN32)
        if (winsock_started) {
            static_cast<void>(WSACleanup());
        }
#endif
    }

    static slirp_ssize_t send_packet_callback(
        const void* buffer,
        std::size_t length,
        void* opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        if (buffer == nullptr || length == 0 ||
            length > devices::VirtioNet::maximum_frame_size ||
            self.received_frames.size() >= maximum_queued_frames) {
            ++self.statistics.dropped_host_frames;
            return static_cast<slirp_ssize_t>(length);
        }

        const auto* const bytes =
            static_cast<const std::uint8_t*>(buffer);
        self.received_frames.emplace_back(bytes, bytes + length);
        self.statistics.queued_host_frames =
            self.received_frames.size();
        self.statistics.peak_queued_host_frames = std::max(
            self.statistics.peak_queued_host_frames,
            self.statistics.queued_host_frames);
        ++self.statistics.host_to_guest_frames;
        self.statistics.host_to_guest_bytes += length;
        return static_cast<slirp_ssize_t>(length);
    }

    static void guest_error_callback(
        const char* message,
        void* opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        ++self.statistics.guest_errors;
        self.last_guest_error =
            message == nullptr ? "unknown guest network error" : message;
    }

    static std::int64_t clock_get_ns_callback(void* opaque)
    {
        static_cast<void>(opaque);
        return monotonic_nanoseconds();
    }

    static void* timer_new_callback(
        SlirpTimerCb callback,
        void* callback_opaque,
        void* opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        auto* const timer = new Timer{
            .callback = callback,
            .callback_opaque = callback_opaque,
        };
        self.timers.push_back(timer);
        return timer;
    }

    static void timer_free_callback(void* timer, void* opaque)
    {
        auto& self = *static_cast<Impl*>(opaque);
        auto* const entry = static_cast<Timer*>(timer);
        const auto found =
            std::find(self.timers.begin(), self.timers.end(), entry);
        if (found != self.timers.end()) {
            self.timers.erase(found);
        }
        delete entry;
    }

    static void timer_mod_callback(
        void* timer,
        std::int64_t expire_time,
        void* opaque)
    {
        static_cast<void>(opaque);
        auto& entry = *static_cast<Timer*>(timer);
        entry.expires_ms = expire_time;
        entry.enabled = true;
    }

    static void notify_callback(void* opaque)
    {
        static_cast<Impl*>(opaque)->poll_requested = true;
    }

    static void register_poll_socket_callback(
        slirp_os_socket socket,
        void* opaque)
    {
        static_cast<void>(socket);
        auto& self = *static_cast<Impl*>(opaque);
        ++self.statistics.poll_socket_registrations;
        self.poll_requested = true;
    }

    static void unregister_poll_socket_callback(
        slirp_os_socket socket,
        void* opaque)
    {
        static_cast<void>(socket);
        auto& self = *static_cast<Impl*>(opaque);
        ++self.statistics.poll_socket_unregistrations;
        self.poll_requested = true;
    }

    static int add_poll_callback(
        slirp_os_socket socket,
        int events,
        void* opaque)
    {
        auto& context = *static_cast<PollContext*>(opaque);
        PollEntry entry{.socket = socket};
#if defined(_WIN32)
        entry.descriptor.fd = socket;
        entry.descriptor.events = 0;
        if ((events & SLIRP_POLL_IN) != 0) {
            entry.descriptor.events |= POLLRDNORM;
        }
        if ((events & SLIRP_POLL_OUT) != 0) {
            entry.descriptor.events |= POLLWRNORM;
        }
        if ((events & SLIRP_POLL_PRI) != 0) {
            entry.descriptor.events |= POLLPRI;
        }
#else
        entry.descriptor.fd = socket;
        entry.descriptor.events = 0;
        if ((events & SLIRP_POLL_IN) != 0) {
            entry.descriptor.events |= POLLIN;
        }
        if ((events & SLIRP_POLL_OUT) != 0) {
            entry.descriptor.events |= POLLOUT;
        }
        if ((events & SLIRP_POLL_PRI) != 0) {
            entry.descriptor.events |= POLLPRI;
        }
#endif
        context.entries.push_back(entry);
        return static_cast<int>(context.entries.size() - 1U);
    }

    static int get_revents_callback(int index, void* opaque)
    {
        const auto& context = *static_cast<PollContext*>(opaque);
        if (index < 0 ||
            static_cast<std::size_t>(index) >= context.entries.size()) {
            return 0;
        }
        const auto revents =
            context.entries[static_cast<std::size_t>(index)]
                .descriptor.revents;
        int result = 0;
#if defined(_WIN32)
        if ((revents & (POLLRDNORM | POLLIN)) != 0) {
            result |= SLIRP_POLL_IN;
        }
        if ((revents & (POLLWRNORM | POLLOUT)) != 0) {
            result |= SLIRP_POLL_OUT;
        }
#else
        if ((revents & POLLIN) != 0) {
            result |= SLIRP_POLL_IN;
        }
        if ((revents & POLLOUT) != 0) {
            result |= SLIRP_POLL_OUT;
        }
#endif
        if ((revents & POLLPRI) != 0) {
            result |= SLIRP_POLL_PRI;
        }
        if ((revents & POLLERR) != 0) {
            result |= SLIRP_POLL_ERR;
        }
        if ((revents & POLLHUP) != 0) {
            result |= SLIRP_POLL_HUP;
        }
        return result;
    }

    void fire_expired_timers()
    {
        const auto now_ms = monotonic_nanoseconds() / 1'000'000;
        std::vector<Timer*> expired;
        for (auto* timer : timers) {
            if (timer->enabled && timer->expires_ms <= now_ms) {
                timer->enabled = false;
                expired.push_back(timer);
            }
        }
        for (auto* timer : expired) {
            if (std::find(timers.begin(), timers.end(), timer) !=
                    timers.end() &&
                timer->callback != nullptr) {
                timer->callback(timer->callback_opaque);
            }
        }
    }

    void poll_once()
    {
        if (slirp == nullptr) {
            return;
        }
        ++statistics.poll_calls;
        poll_requested = false;
        fire_expired_timers();

        PollContext context;
        std::uint32_t timeout = 0;
        slirp_pollfds_fill_socket(
            slirp,
            &timeout,
            &add_poll_callback,
            &context);
        statistics.poll_socket_observations += context.entries.size();

        int result = 0;
        if (!context.entries.empty()) {
#if defined(_WIN32)
            for (std::size_t offset = 0U;
                 offset < context.entries.size();) {
                const std::size_t end = std::min(
                    offset + static_cast<std::size_t>(FD_SETSIZE),
                    context.entries.size());
                fd_set read_descriptors;
                fd_set write_descriptors;
                fd_set exception_descriptors;
                FD_ZERO(&read_descriptors);
                FD_ZERO(&write_descriptors);
                FD_ZERO(&exception_descriptors);
                for (std::size_t index = offset; index < end; ++index) {
                    auto& entry = context.entries[index];
                    entry.descriptor.revents = 0;
                    if ((entry.descriptor.events &
                         (POLLRDNORM | POLLIN)) != 0) {
                        FD_SET(entry.socket, &read_descriptors);
                    }
                    if ((entry.descriptor.events &
                         (POLLWRNORM | POLLOUT)) != 0) {
                        FD_SET(entry.socket, &write_descriptors);
                    }
                    FD_SET(entry.socket, &exception_descriptors);
                }
                timeval wait{};
                const int batch_result = select(
                    0,
                    &read_descriptors,
                    &write_descriptors,
                    &exception_descriptors,
                    &wait);
                if (batch_result == SOCKET_ERROR) {
                    result = SOCKET_ERROR;
                    statistics.last_poll_error = WSAGetLastError();
                    break;
                }
                result += batch_result;
                for (std::size_t index = offset; index < end; ++index) {
                    auto& entry = context.entries[index];
                    if (FD_ISSET(entry.socket, &read_descriptors)) {
                        entry.descriptor.revents |= POLLRDNORM;
                    }
                    if (FD_ISSET(entry.socket, &write_descriptors)) {
                        entry.descriptor.revents |= POLLWRNORM;
                    }
                    if (FD_ISSET(
                            entry.socket,
                            &exception_descriptors)) {
                        if ((entry.descriptor.events & POLLPRI) != 0) {
                            entry.descriptor.revents |= POLLPRI;
                        } else {
                            entry.descriptor.revents |= POLLERR;
                        }
                    }
                }
                offset = end;
            }
#else
            std::vector<pollfd>
                descriptors;
            descriptors.reserve(context.entries.size());
            for (const auto& entry : context.entries) {
                descriptors.push_back(entry.descriptor);
            }
            result = poll(
                descriptors.data(),
                static_cast<nfds_t>(descriptors.size()),
                0);
            for (std::size_t index = 0;
                 index < descriptors.size();
                 ++index) {
                context.entries[index].descriptor.revents =
                    descriptors[index].revents;
            }
#endif
        }
        if (result > 0) {
            statistics.poll_ready_events +=
                static_cast<std::uint64_t>(result);
        } else if (result < 0) {
            ++statistics.poll_errors;
        }

        slirp_pollfds_poll(
            slirp,
            result < 0 ? 1 : 0,
            &get_revents_callback,
            &context);
        fire_expired_timers();
    }

    SlirpConfig config{};
    SlirpCb callbacks{};
    Slirp* slirp{};
    std::deque<std::vector<std::uint8_t>> received_frames;
    std::vector<Timer*> timers;
    std::string error;
    std::string last_guest_error;
    std::uint64_t accumulated_cycles{};
    bool poll_requested{true};
#if defined(_WIN32)
    bool winsock_started{};
#endif
    SlirpNetworkStatistics statistics{};
};

SlirpNetworkBackend::SlirpNetworkBackend()
    : impl_(std::make_unique<Impl>())
{
}

SlirpNetworkBackend::~SlirpNetworkBackend() = default;

void SlirpNetworkBackend::tick(std::uint64_t cycles)
{
    if (impl_->accumulated_cycles >
        std::numeric_limits<std::uint64_t>::max() - cycles) {
        impl_->accumulated_cycles = poll_interval_cycles;
    } else {
        impl_->accumulated_cycles += cycles;
    }
    if (!impl_->poll_requested &&
        impl_->accumulated_cycles < poll_interval_cycles) {
        return;
    }
    impl_->accumulated_cycles = 0;
    impl_->poll_once();
}

void SlirpNetworkBackend::send_frame(
    std::span<const std::uint8_t> frame)
{
    if (impl_->slirp == nullptr || frame.empty() ||
        frame.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max())) {
        return;
    }
    slirp_input(
        impl_->slirp,
        frame.data(),
        static_cast<int>(frame.size()));
    ++impl_->statistics.guest_to_host_frames;
    impl_->statistics.guest_to_host_bytes += frame.size();
    impl_->poll_requested = true;
}

std::optional<std::vector<std::uint8_t>>
SlirpNetworkBackend::receive_frame()
{
    if (impl_->received_frames.empty()) {
        return std::nullopt;
    }
    auto frame = std::move(impl_->received_frames.front());
    impl_->received_frames.pop_front();
    impl_->statistics.queued_host_frames =
        impl_->received_frames.size();
    return frame;
}

bool SlirpNetworkBackend::ready() const noexcept
{
    return impl_->slirp != nullptr;
}

std::string_view SlirpNetworkBackend::error() const noexcept
{
    return impl_->error;
}

std::string_view SlirpNetworkBackend::version() const noexcept
{
    return slirp_version_string();
}

const SlirpNetworkStatistics&
SlirpNetworkBackend::statistics() const noexcept
{
    return impl_->statistics;
}

} // namespace rv::app
