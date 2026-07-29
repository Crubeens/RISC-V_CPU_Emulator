#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <system_error>

#include "rv/devices/ram.hpp"
#include "rv32/platform/machine.hpp"

namespace {

constexpr std::uint64_t default_steps = 1'000'000U;

struct BenchmarkResult {
    double seconds{};
    double million_steps_per_second{};
    rv32::CorePerformanceCounters core{};
    rv::platform::BusPerformanceCounters bus{};
};

[[nodiscard]] bool parse_steps(
    std::string_view text,
    std::uint64_t& value) noexcept
{
    const auto result = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value);
    return result.ec == std::errc{} &&
           result.ptr == text.data() + text.size() &&
           value != 0U;
}

[[nodiscard]] BenchmarkResult run(
    rv32::ExecutionMode mode,
    std::uint64_t steps)
{
    rv32::platform::MachineConfig config;
    config.ram_size = 1024U * 1024U;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;
    rv32::platform::Machine machine(config);
    machine.core().set_execution_mode(mode);

    // jal x0, 0 repeatedly executes one valid instruction without involving
    // host I/O or device state. This makes decoder and dispatch regressions
    // reproducible across runs.
    constexpr std::array<std::uint8_t, 4> loop{
        0x6FU, 0x00U, 0x00U, 0x00U};
    if (machine.ram().load_image(loop) != rv32::BusFault::None) {
        return {};
    }

    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t step = 0; step < steps; ++step) {
        const auto result = machine.step();
        if (result.status != rv32::StepStatus::Retired) {
            return {};
        }
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started)
                               .count();
    return {
        .seconds = seconds,
        .million_steps_per_second =
            seconds == 0.0
                ? 0.0
                : static_cast<double>(steps) / seconds / 1'000'000.0,
        .core = machine.core().performance_counters(),
        .bus = machine.bus().performance_counters(),
    };
}

void print(
    std::string_view name,
    const BenchmarkResult& result)
{
    std::cout
        << name
        << ": seconds=" << result.seconds
        << ", Msteps/s=" << result.million_steps_per_second
        << ", decode-hit=" << result.core.decode.hits
        << ", decode-miss=" << result.core.decode.misses
        << ", instruction-cache-hit="
        << result.core.instruction_cache.hits
        << ", instruction-cache-miss="
        << result.core.instruction_cache.misses
        << ", bus-cache-hit=" << result.bus.device_cache_hits
        << '/' << result.bus.device_lookups << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    std::uint64_t steps = default_steps;
    if (argc > 2 ||
        (argc == 2 && !parse_steps(argv[1], steps))) {
        std::cerr << "usage: rv32_performance_runner [positive-steps]\n";
        return 2;
    }

    const auto reference =
        run(rv32::ExecutionMode::Reference, steps);
    const auto fast = run(rv32::ExecutionMode::Fast, steps);
    if (reference.core.retired_instructions != steps ||
        fast.core.retired_instructions != steps ||
        reference.seconds <= 0.0 ||
        fast.seconds <= 0.0) {
        std::cerr << "performance benchmark did not complete\n";
        return 1;
    }

    print("reference", reference);
    print("fast", fast);
    std::cout
        << "speedup="
        << fast.million_steps_per_second /
               reference.million_steps_per_second
        << "x\n";
    return 0;
}
