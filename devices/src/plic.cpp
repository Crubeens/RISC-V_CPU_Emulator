#include "rv32/devices/plic.hpp"

#include <algorithm>

namespace rv32::devices {

Plic::Plic(PhysAddr base, std::uint64_t size)
    : range_{.base = base, .size = size}
{
}

std::string_view Plic::name() const noexcept
{
    return "PLIC";
}

platform::AddressRange Plic::range() const noexcept
{
    return range_;
}

ReadResult Plic::read(
    std::uint64_t offset,
    AccessWidth width)
{
    if (width != AccessWidth::Word || (offset & 0x3U) != 0) {
        return {.fault = BusFault::Unsupported};
    }

    if (offset < source_slots * sizeof(std::uint32_t)) {
        const auto source =
            static_cast<std::size_t>(offset / sizeof(std::uint32_t));
        return {
            .fault = BusFault::None,
            .value = priorities_[source],
        };
    }

    const auto pending_size =
        pending_word_count * sizeof(std::uint32_t);
    if (offset >= pending_base &&
        offset < pending_base + pending_size) {
        const auto word = static_cast<std::size_t>(
            (offset - pending_base) / sizeof(std::uint32_t));
        return {
            .fault = BusFault::None,
            .value = pending_[word],
        };
    }

    for (std::uint32_t context = 0;
         context < context_count;
         ++context) {
        const auto context_enable =
            enable_base +
            static_cast<std::uint64_t>(context) * enable_stride;
        if (offset >= context_enable &&
            offset < context_enable + pending_size) {
            const auto word = static_cast<std::size_t>(
                (offset - context_enable) / sizeof(std::uint32_t));
            return {
                .fault = BusFault::None,
                .value = enables_[context][word],
            };
        }

        const auto context_registers =
            context_base +
            static_cast<std::uint64_t>(context) * context_stride;
        if (offset == context_registers) {
            return {
                .fault = BusFault::None,
                .value = thresholds_[context],
            };
        }
        if (offset == context_registers + claim_offset) {
            return {
                .fault = BusFault::None,
                .value = claim(context),
            };
        }
    }

    return {.fault = BusFault::None, .value = 0};
}

BusFault Plic::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    if (width != AccessWidth::Word || (offset & 0x3U) != 0) {
        return BusFault::Unsupported;
    }

    if (offset < source_slots * sizeof(std::uint32_t)) {
        const auto source =
            static_cast<std::size_t>(offset / sizeof(std::uint32_t));
        if (source != 0) {
            priorities_[source] =
                static_cast<std::uint32_t>(value) & 0x7U;
            refresh_all_pending();
        }
        return BusFault::None;
    }

    const auto pending_size =
        pending_word_count * sizeof(std::uint32_t);

    for (std::uint32_t context = 0;
         context < context_count;
         ++context) {
        const auto context_enable =
            enable_base +
            static_cast<std::uint64_t>(context) * enable_stride;
        if (offset >= context_enable &&
            offset < context_enable + pending_size) {
            const auto word = static_cast<std::size_t>(
                (offset - context_enable) / sizeof(std::uint32_t));
            enables_[context][word] =
                static_cast<std::uint32_t>(value);
            enables_[context][0] &= ~1U;
            refresh_pending(context);
            return BusFault::None;
        }

        const auto context_registers =
            context_base +
            static_cast<std::uint64_t>(context) * context_stride;
        if (offset == context_registers) {
            thresholds_[context] =
                static_cast<std::uint32_t>(value) & 0x7U;
            refresh_pending(context);
            return BusFault::None;
        }
        if (offset == context_registers + claim_offset) {
            complete(
                context,
                static_cast<std::uint32_t>(value));
            return BusFault::None;
        }
    }

    return BusFault::None;
}

void Plic::set_source_level(
    std::uint32_t source,
    bool asserted) noexcept
{
    if (source == 0 || source > source_count) {
        return;
    }

    const auto index = static_cast<std::size_t>(source);
    const bool was_asserted = source_levels_[index];
    source_levels_[index] = asserted;

    if (asserted && !was_asserted && claimed_[0] != source &&
        claimed_[1] != source) {
        set_pending(source, true);
    }
}

bool Plic::machine_external_irq() const noexcept
{
    return best_pending(machine_context) != 0;
}

bool Plic::supervisor_external_irq() const noexcept
{
    return best_pending(supervisor_context) != 0;
}

bool Plic::source_enabled(
    std::uint32_t context,
    std::uint32_t source) const noexcept
{
    if (context >= context_count ||
        source == 0 ||
        source > source_count) {
        return false;
    }

    const auto word = static_cast<std::size_t>(source / 32U);
    const auto bit = source % 32U;
    return ((enables_[context][word] >> bit) & 1U) != 0;
}

bool Plic::source_pending(std::uint32_t source) const noexcept
{
    if (source == 0 || source > source_count) {
        return false;
    }

    const auto word = static_cast<std::size_t>(source / 32U);
    const auto bit = source % 32U;
    return ((pending_[word] >> bit) & 1U) != 0;
}

void Plic::set_pending(
    std::uint32_t source,
    bool pending) noexcept
{
    if (source == 0 || source > source_count) {
        return;
    }

    const auto word = static_cast<std::size_t>(source / 32U);
    const auto bit = source % 32U;
    const bool was_pending = source_pending(source);
    if (pending) {
        pending_[word] |= std::uint32_t{1} << bit;
    } else {
        pending_[word] &= ~(std::uint32_t{1} << bit);
    }
    if (pending != was_pending) {
        refresh_all_pending();
    }
}

std::uint32_t Plic::best_pending(
    std::uint32_t context) const noexcept
{
    if (context >= context_count) {
        return 0;
    }
    return best_pending_cache_[context];
}

std::uint32_t Plic::compute_best_pending(
    std::uint32_t context) const noexcept
{
    if (context >= context_count) {
        return 0;
    }

    std::uint32_t best_source = 0;
    std::uint32_t best_priority = 0;

    for (std::uint32_t source = 1;
         source <= source_count;
         ++source) {
        const auto priority =
            priorities_[static_cast<std::size_t>(source)];
        if (!source_pending(source) ||
            !source_enabled(context, source) ||
            priority <= thresholds_[context]) {
            continue;
        }

        if (priority > best_priority ||
            (priority == best_priority &&
             (best_source == 0 || source < best_source))) {
            best_source = source;
            best_priority = priority;
        }
    }

    return best_source;
}

void Plic::refresh_pending(std::uint32_t context) noexcept
{
    if (context < context_count) {
        best_pending_cache_[context] =
            compute_best_pending(context);
    }
}

void Plic::refresh_all_pending() noexcept
{
    for (std::uint32_t context = 0;
         context < context_count;
         ++context) {
        refresh_pending(context);
    }
}

std::uint32_t Plic::claim(std::uint32_t context) noexcept
{
    const auto source = best_pending(context);
    if (source != 0) {
        set_pending(source, false);
        claimed_[context] = source;
    }
    return source;
}

void Plic::complete(
    std::uint32_t context,
    std::uint32_t source) noexcept
{
    if (context >= context_count ||
        source == 0 ||
        source > source_count ||
        claimed_[context] != source) {
        return;
    }

    claimed_[context] = 0;
    if (source_levels_[static_cast<std::size_t>(source)]) {
        set_pending(source, true);
    }
}

} // namespace rv32::devices
