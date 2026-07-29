#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "rv/devices/framebuffer.hpp"

namespace {

constexpr std::uint32_t width = 640U;
constexpr std::uint32_t height = 480U;
constexpr std::uint32_t bytes_per_pixel = 4U;
constexpr rv::PhysAddr base = 0x40000000ULL;

struct BenchmarkResult {
    std::string_view name;
    double seconds{};
    std::uint64_t bytes{};
    rv::devices::FramebufferDirtyRegion dirty{};
    bool passed{};
};

template <typename Operation>
[[nodiscard]] BenchmarkResult run(
    std::string_view name,
    rv::devices::Framebuffer& framebuffer,
    std::uint64_t bytes,
    Operation operation)
{
    framebuffer.clear_dirty();
    const auto started = std::chrono::steady_clock::now();
    const bool passed = operation();
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started)
                               .count();
    return {
        .name = name,
        .seconds = seconds,
        .bytes = bytes,
        .dirty = framebuffer.dirty_region(),
        .passed = passed,
    };
}

[[nodiscard]] bool write_word(
    rv::devices::Framebuffer& framebuffer,
    std::uint64_t pixel,
    std::uint32_t value)
{
    return framebuffer.write(
               pixel * bytes_per_pixel,
               rv::AccessWidth::Word,
               value) == rv::BusFault::None;
}

[[nodiscard]] BenchmarkResult full_fill(
    rv::devices::Framebuffer& framebuffer,
    std::uint32_t frames)
{
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * height;
    return run(
        "full-fill",
        framebuffer,
        pixels * bytes_per_pixel * frames,
        [&] {
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const std::uint32_t color =
                    0x00010101U * (frame + 1U);
                for (std::uint64_t pixel = 0; pixel < pixels; ++pixel) {
                    if (!write_word(framebuffer, pixel, color)) {
                        return false;
                    }
                }
            }
            return true;
        });
}

[[nodiscard]] BenchmarkResult full_copy(
    rv::devices::Framebuffer& framebuffer,
    std::uint32_t frames)
{
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * height;
    return run(
        "full-copy",
        framebuffer,
        pixels * bytes_per_pixel * 2U * frames,
        [&] {
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                for (std::uint64_t pixel = 1; pixel < pixels; ++pixel) {
                    const auto source = framebuffer.read(
                        (pixel - 1U) * bytes_per_pixel,
                        rv::AccessWidth::Word);
                    if (!source.ok() ||
                        !write_word(
                            framebuffer,
                            pixel,
                            static_cast<std::uint32_t>(source.value))) {
                        return false;
                    }
                }
            }
            return true;
        });
}

[[nodiscard]] BenchmarkResult text_scroll(
    rv::devices::Framebuffer& framebuffer,
    std::uint32_t scrolls)
{
    constexpr std::uint32_t glyph_height = 16U;
    const std::uint64_t copied_pixels =
        static_cast<std::uint64_t>(height - glyph_height) * width;
    const std::uint64_t cleared_pixels =
        static_cast<std::uint64_t>(glyph_height) * width;
    return run(
        "text-scroll",
        framebuffer,
        (copied_pixels * 2U + cleared_pixels) *
            bytes_per_pixel * scrolls,
        [&] {
            for (std::uint32_t scroll = 0; scroll < scrolls; ++scroll) {
                for (std::uint64_t pixel = 0;
                     pixel < copied_pixels;
                     ++pixel) {
                    const auto source = framebuffer.read(
                        (pixel + cleared_pixels) * bytes_per_pixel,
                        rv::AccessWidth::Word);
                    if (!source.ok() ||
                        !write_word(
                            framebuffer,
                            pixel,
                            static_cast<std::uint32_t>(source.value))) {
                        return false;
                    }
                }
                for (std::uint64_t pixel = copied_pixels;
                     pixel < copied_pixels + cleared_pixels;
                     ++pixel) {
                    if (!write_word(framebuffer, pixel, 0U)) {
                        return false;
                    }
                }
            }
            return true;
        });
}

[[nodiscard]] BenchmarkResult local_animation(
    rv::devices::Framebuffer& framebuffer,
    std::uint32_t frames)
{
    constexpr std::uint32_t side = 64U;
    constexpr std::uint32_t origin_x = 32U;
    constexpr std::uint32_t origin_y = 32U;
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(side) * side;
    return run(
        "local-64x64",
        framebuffer,
        pixels * bytes_per_pixel * frames,
        [&] {
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                for (std::uint32_t y = 0; y < side; ++y) {
                    for (std::uint32_t x = 0; x < side; ++x) {
                        const std::uint64_t pixel =
                            static_cast<std::uint64_t>(origin_y + y) *
                                width +
                            origin_x + x;
                        if (!write_word(
                                framebuffer,
                                pixel,
                                frame ^ (x << 8U) ^ y)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        });
}

void print(const BenchmarkResult& result)
{
    const double mebibytes_per_second =
        result.seconds == 0.0
            ? 0.0
            : static_cast<double>(result.bytes) /
                  (1024.0 * 1024.0) / result.seconds;
    const double dirty_ratio =
        100.0 * result.dirty.width * result.dirty.height /
        static_cast<double>(width * height);
    std::cout
        << result.name
        << ": seconds=" << std::fixed << std::setprecision(6)
        << result.seconds
        << ", MiB/s=" << std::setprecision(2)
        << mebibytes_per_second
        << ", dirty=" << result.dirty.width << 'x'
        << result.dirty.height
        << ", dirty-ratio=" << dirty_ratio << "%\n";
}

} // namespace

int main(int argc, char** argv)
{
    const bool smoke =
        argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (argc > 2 || (argc == 2 && !smoke)) {
        std::cerr << "usage: rv32_framebuffer_benchmark [--smoke]\n";
        return 2;
    }

    rv::devices::Framebuffer framebuffer(
        base,
        width,
        height,
        bytes_per_pixel);
    const std::uint32_t scale = smoke ? 1U : 8U;
    const BenchmarkResult results[]{
        full_fill(framebuffer, scale),
        full_copy(framebuffer, scale),
        text_scroll(framebuffer, scale),
        local_animation(framebuffer, scale * 30U),
    };

    bool passed = true;
    for (const auto& result : results) {
        print(result);
        passed = passed && result.passed &&
                 result.seconds > 0.0 &&
                 !result.dirty.empty();
    }
    return passed ? 0 : 1;
}
