#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace rv32::devices {
class Framebuffer;
}

namespace rv32::app {

enum class DisplayView {
    Terminal,
    Framebuffer,
};

struct SdlPerformanceCounters {
    std::uint64_t presented_frames{};
    std::uint64_t deferred_updates{};
    std::uint64_t full_texture_uploads{};
    std::uint64_t partial_texture_uploads{};
    std::uint64_t uploaded_bytes{};
    std::uint64_t presentation_span_ms{};
};

class SdlFrontend {
  public:
    SdlFrontend();
    ~SdlFrontend();

    SdlFrontend(const SdlFrontend&) = delete;
    SdlFrontend& operator=(const SdlFrontend&) = delete;
    SdlFrontend(SdlFrontend&&) = delete;
    SdlFrontend& operator=(SdlFrontend&&) = delete;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::string_view error() const noexcept;
    [[nodiscard]] DisplayView view() const noexcept;
    [[nodiscard]] const SdlPerformanceCounters&
    performance_counters() const noexcept;

    void append_uart(std::string_view output);
    [[nodiscard]] std::string poll_input();
    void present(devices::Framebuffer* framebuffer);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rv32::app
