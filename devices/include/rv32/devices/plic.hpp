#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "rv32/platform/device.hpp"

namespace rv32::devices {

class Plic final : public platform::Device {
  public:
    static constexpr std::uint32_t source_count = 53;
    static constexpr std::uint32_t context_count = 2;
    static constexpr std::uint32_t machine_context = 0;
    static constexpr std::uint32_t supervisor_context = 1;

    Plic(PhysAddr base, std::uint64_t size);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] platform::AddressRange range() const noexcept override;

    [[nodiscard]] ReadResult read(
        std::uint64_t offset,
        AccessWidth width) override;

    [[nodiscard]] BusFault write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value) override;

    void set_source_level(std::uint32_t source, bool asserted) noexcept;

    [[nodiscard]] bool machine_external_irq() const noexcept;
    [[nodiscard]] bool supervisor_external_irq() const noexcept;

  private:
    static constexpr std::uint64_t pending_base = 0x001000;
    static constexpr std::uint64_t enable_base = 0x002000;
    static constexpr std::uint64_t enable_stride = 0x000080;
    static constexpr std::uint64_t context_base = 0x200000;
    static constexpr std::uint64_t context_stride = 0x001000;
    static constexpr std::uint64_t claim_offset = 0x4;

    static constexpr std::size_t source_slots =
        static_cast<std::size_t>(source_count) + 1U;
    static constexpr std::size_t pending_word_count =
        (source_slots + 31U) / 32U;

    [[nodiscard]] bool source_enabled(
        std::uint32_t context,
        std::uint32_t source) const noexcept;

    [[nodiscard]] bool source_pending(
        std::uint32_t source) const noexcept;

    void set_pending(std::uint32_t source, bool pending) noexcept;

    [[nodiscard]] std::uint32_t best_pending(
        std::uint32_t context) const noexcept;

    [[nodiscard]] std::uint32_t compute_best_pending(
        std::uint32_t context) const noexcept;

    void refresh_pending(std::uint32_t context) noexcept;
    void refresh_all_pending() noexcept;

    [[nodiscard]] std::uint32_t claim(
        std::uint32_t context) noexcept;

    void complete(
        std::uint32_t context,
        std::uint32_t source) noexcept;

    platform::AddressRange range_;
    std::array<std::uint32_t, source_slots> priorities_{};
    std::array<std::uint32_t, pending_word_count> pending_{};
    std::array<
        std::array<std::uint32_t, pending_word_count>,
        context_count>
        enables_{};
    std::array<std::uint32_t, context_count> thresholds_{};
    std::array<std::uint32_t, context_count> claimed_{};
    std::array<bool, source_slots> source_levels_{};
    std::array<std::uint32_t, context_count> best_pending_cache_{};
};

} // namespace rv32::devices
