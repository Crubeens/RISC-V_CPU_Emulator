#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <system_error>

#include "rv/devices/ram.hpp"
#include "rv64/platform/machine.hpp"

namespace {

constexpr std::uint64_t default_steps = 1'000'000U;

struct BenchmarkResult {
    double seconds{};
    double million_steps_per_second{};
    rv64::CpuSnapshot snapshot{};
    rv64::CorePerformanceCounters core{};
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
    rv64::ExecutionMode mode,
    std::uint64_t steps)
{
    rv64::platform::Machine machine({
        .ram_size = 1024U * 1024U,
        .virtual_disk_size = 512U,
        .enable_framebuffer = false,
    });
    machine.core().set_execution_mode(mode);

    // jal x0, 0 is deterministic, never touches a device, and exercises
    // instruction translation, fetch, decode, and dispatch on every step.
    constexpr std::array<std::uint8_t, 4> loop{
        0x6FU, 0x00U, 0x00U, 0x00U};
    if (machine.ram().load_image(loop) != rv::BusFault::None) {
        return {};
    }
    machine.reset({
        .reset_pc = rv64::platform::address_map::dram_base,
    });

    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t step = 0; step < steps; ++step) {
        const auto result = machine.step();
        if (result.status != rv64::StepStatus::Retired) {
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
        .snapshot = machine.core().snapshot(),
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
        << ", fetch-halfwords=" << result.core.fetch.halfword_reads
        << ", translations=" << result.core.mmu.translations
        << ", TLB-hit=" << result.core.mmu.tlb_hits
        << ", TLB-miss=" << result.core.mmu.tlb_misses
        << ", bus-cache-hit=" << result.bus.device_cache_hits
        << '/' << result.bus.device_lookups << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    std::uint64_t steps = default_steps;
    if (argc > 2 ||
        (argc == 2 && !parse_steps(argv[1], steps))) {
        std::cerr << "usage: rv64_performance_runner [positive-steps]\n";
        return 2;
    }

    const auto reference =
        run(rv64::ExecutionMode::Reference, steps);
    const auto fast = run(rv64::ExecutionMode::Fast, steps);
    if (reference.core.retired_instructions != steps ||
        fast.core.retired_instructions != steps ||
        reference.snapshot != fast.snapshot ||
        reference.seconds <= 0.0 ||
        fast.seconds <= 0.0 ||
        reference.core.decode.lookups != 0U ||
        fast.core.instruction_cache.hits == 0U ||
        fast.core.instruction_cache.misses == 0U ||
        fast.core.fetch.halfword_reads >=
            reference.core.fetch.halfword_reads) {
        std::cerr
            << "RV64 performance/reference comparison did not complete\n";
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
