#include "rv64/app/cli.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#include "rv/devices/framebuffer.hpp"
#include "rv/devices/syscon.hpp"
#include "rv/devices/uart16550.hpp"
#include "rv/devices/virtio_block.hpp"
#include "rv/devices/virtio_net.hpp"
#include "rv64/platform/device_tree.hpp"
#include "rv64/platform/machine.hpp"

#if defined(RV_ENABLE_NETWORK)
#include "rv/app/slirp_network_backend.hpp"
#endif

#if defined(RV_ENABLE_SDL)
#include "rv/app/sdl_frontend.hpp"
#endif

namespace rv64::app {

namespace {

constexpr std::uint64_t default_step_limit = 1'000'000ULL;
constexpr std::uint64_t default_boot_step_limit = 20'000'000ULL;
constexpr std::uint64_t unlimited_step_limit =
    std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t console_poll_interval = 1024ULL;
constexpr std::uint64_t network_diagnostic_interval =
    100'000'000ULL;
constexpr std::uint64_t bytes_per_mib = 1024ULL * 1024ULL;
constexpr std::uint64_t minimum_ram_mib = 64ULL;
constexpr std::uint64_t maximum_ram_mib = 4096ULL;

[[nodiscard]] bool read_image(
    const char* path,
    std::vector<std::uint8_t>& image)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Cannot open RV64 image: " << path << '\n';
        return false;
    }
    image.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (input.bad() || image.empty()) {
        std::cerr << "Cannot read RV64 image: " << path << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool synchronize_disk(
    platform::Machine& machine,
    const char* path)
{
    auto& disk = machine.virtio_block();
    if (path == nullptr || !disk.dirty()) {
        return true;
    }
    if (!disk.flush()) {
        std::cerr
            << "Cannot synchronize RV64 virtual disk: "
            << path << '\n';
        return false;
    }
    disk.clear_dirty();
    std::cout
        << "RV64 file-backed virtual disk synchronized: "
        << path << '\n';
    return true;
}

void forward_console_input(platform::Machine& machine)
{
    std::array<char, 256> input{};
    std::size_t size = 0;

#if defined(_WIN32)
    while (size < input.size() && _kbhit() != 0) {
        const int character = _getch();
        if (character == 0 || character == 0xE0) {
            if (_kbhit() != 0) {
                static_cast<void>(_getch());
            }
            continue;
        }
        input[size] = static_cast<char>(character);
        ++size;
    }
#else
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(STDIN_FILENO, &descriptors);
    timeval timeout{};
    const int ready = select(
        STDIN_FILENO + 1,
        &descriptors,
        nullptr,
        nullptr,
        &timeout);
    if (ready > 0 && FD_ISSET(STDIN_FILENO, &descriptors)) {
        const auto count =
            read(STDIN_FILENO, input.data(), input.size());
        if (count > 0) {
            size = static_cast<std::size_t>(count);
        }
    }
#endif

    if (size != 0U) {
        machine.uart().inject_received(
            std::string_view(input.data(), size));
    }
}

void print_performance_diagnostics(
    platform::Machine& machine,
    std::chrono::steady_clock::duration elapsed)
{
    const double seconds =
        std::chrono::duration<double>(elapsed).count();
    const auto& core = machine.core().performance_counters();
    const auto& mmu = core.mmu;
    const auto& decode = core.decode;
    const auto& instruction_cache = core.instruction_cache;
    const auto& fetch = core.fetch;
    const auto& bus = machine.bus().performance_counters();
    const double steps_per_second =
        seconds > 0.0
            ? static_cast<double>(core.step_calls) / seconds
            : 0.0;
    const double tlb_hit_rate =
        mmu.tlb_hits + mmu.tlb_misses == 0U
            ? 0.0
            : 100.0 * static_cast<double>(mmu.tlb_hits) /
                  static_cast<double>(
                      mmu.tlb_hits + mmu.tlb_misses);
    const double decode_hit_rate =
        decode.hits + decode.misses == 0U
            ? 0.0
            : 100.0 * static_cast<double>(decode.hits) /
                  static_cast<double>(
                      decode.hits + decode.misses);
    const double instruction_cache_hit_rate =
        instruction_cache.hits + instruction_cache.misses == 0U
            ? 0.0
            : 100.0 *
                  static_cast<double>(instruction_cache.hits) /
                  static_cast<double>(
                      instruction_cache.hits +
                      instruction_cache.misses);
    const auto read_count = [&](rv::AccessKind kind) {
        return bus.reads[static_cast<std::size_t>(kind)];
    };
    const auto write_count = [&](rv::AccessKind kind) {
        return bus.writes[static_cast<std::size_t>(kind)];
    };

    std::cerr
        << std::fixed << std::setprecision(3)
        << "RV64 performance statistics:\n"
        << "  mode="
        << (machine.core().execution_mode() ==
                    ExecutionMode::Fast
                ? "fast"
                : "reference")
        << ", elapsed=" << seconds
        << "s, steps=" << core.step_calls
        << ", throughput=" << steps_per_second / 1'000'000.0
        << " Msteps/s, retired=" << core.retired_instructions
        << '\n'
        << "  traps: synchronous=" << core.synchronous_traps
        << ", interrupt=" << core.interrupt_traps
        << ", WFI-idle=" << core.waiting_returns << '\n'
        << "  fetch: instructions=" << fetch.instruction_fetches
        << ", halfword-reads=" << fetch.halfword_reads
        << ", compressed=" << fetch.compressed_instructions
        << ", standard=" << fetch.standard_instructions << '\n'
        << "  TLB: hit=" << mmu.tlb_hits
        << ", miss=" << mmu.tlb_misses
        << ", hit-rate=" << tlb_hit_rate
        << "%, walks=" << mmu.page_table_walks
        << ", PTE-read=" << mmu.pte_reads
        << ", PTE-write=" << mmu.pte_writes << '\n'
        << "  decode-cache: hit=" << decode.hits
        << ", miss=" << decode.misses
        << ", hit-rate=" << decode_hit_rate
        << "%, invalidations=" << decode.invalidations << '\n'
        << "  instruction-cache: hit="
        << instruction_cache.hits
        << ", miss=" << instruction_cache.misses
        << ", hit-rate=" << instruction_cache_hit_rate
        << "%, invalidations="
        << instruction_cache.invalidations << '\n'
        << "  bus reads: fetch="
        << read_count(rv::AccessKind::InstructionFetch)
        << ", load=" << read_count(rv::AccessKind::Load)
        << ", walk=" << read_count(rv::AccessKind::PageTableWalk)
        << ", atomic=" << read_count(rv::AccessKind::Atomic)
        << ", DMA=" << read_count(rv::AccessKind::Dma)
        << '\n'
        << "  bus writes: store="
        << write_count(rv::AccessKind::Store)
        << ", walk=" << write_count(rv::AccessKind::PageTableWalk)
        << ", atomic=" << write_count(rv::AccessKind::Atomic)
        << ", DMA=" << write_count(rv::AccessKind::Dma)
        << ", faults=" << bus.faults << '\n'
        << "  bus device lookup cache: "
        << bus.device_cache_hits << '/' << bus.device_lookups
        << " hits, device-ticks=" << bus.device_ticks
        << std::defaultfloat << '\n';

    if (const auto* framebuffer = machine.framebuffer();
        framebuffer != nullptr) {
        const auto& statistics = framebuffer->statistics();
        const double guest_megabytes_per_second =
            seconds > 0.0
                ? static_cast<double>(statistics.bytes_written) /
                      seconds / (1024.0 * 1024.0)
                : 0.0;
        std::cerr
            << "  framebuffer: guest-writes="
            << statistics.write_operations
            << ", guest-bytes=" << statistics.bytes_written
            << ", dirty-updates="
            << statistics.dirty_region_updates
            << ", guest-bandwidth=" << std::fixed
            << std::setprecision(3)
            << guest_megabytes_per_second << " MiB/s"
            << std::defaultfloat << '\n';
    }
}

[[nodiscard]] bool parse_limit(
    std::string_view text,
    std::uint64_t& limit)
{
    const auto result = std::from_chars(
        text.data(),
        text.data() + text.size(),
        limit);
    return result.ec == std::errc{} &&
           result.ptr == text.data() + text.size() &&
           limit != 0U;
}

[[nodiscard]] bool parse_ram_size(
    std::string_view text,
    std::uint64_t& size)
{
    std::uint64_t mib = 0U;
    if (!parse_limit(text, mib) ||
        mib < minimum_ram_mib ||
        mib > maximum_ram_mib) {
        return false;
    }
    size = mib * bytes_per_mib;
    return true;
}

[[nodiscard]] bool configure_device_tree(
    std::vector<std::uint8_t>& device_tree,
    std::uint64_t ram_size)
{
    const auto result = platform::patch_device_tree_memory(
        device_tree,
        platform::address_map::dram_base,
        ram_size);
    if (result.ok()) {
        return true;
    }
    std::cerr
        << "Cannot configure RV64 device-tree memory: "
        << platform::device_tree_memory_patch_error_message(result.error)
        << '\n';
    return false;
}

[[nodiscard]] std::unique_ptr<platform::Machine> create_machine(
    const platform::MachineConfig& config,
    std::uint64_t ram_size)
{
    try {
        return std::make_unique<platform::Machine>(config);
    } catch (const std::exception& error) {
        std::cerr
            << "Cannot allocate " << ram_size / bytes_per_mib
            << " MiB of RV64 guest RAM: " << error.what() << '\n';
        return nullptr;
    }
}

int run_smoke()
{
    platform::Machine machine({
        .ram_size = 1ULL * 1024ULL * 1024ULL,
        .virtual_disk_size = 512ULL,
        .enable_framebuffer = false,
    });
    constexpr std::uint32_t nop = 0x00000013U;
    const std::uint8_t image[]{
        static_cast<std::uint8_t>(nop),
        static_cast<std::uint8_t>(nop >> 8U),
        static_cast<std::uint8_t>(nop >> 16U),
        static_cast<std::uint8_t>(nop >> 24U),
    };
    if (machine.load_image(
            image,
            platform::address_map::dram_base) != rv::BusFault::None) {
        std::cerr << "Failed to load the RV64 smoke instruction\n";
        return 1;
    }
    machine.reset({
        .reset_pc = platform::address_map::dram_base,
    });
    const auto result = machine.step();
    const auto state = machine.core().snapshot();
    if (result.status != StepStatus::Retired ||
        state.pc != platform::address_map::dram_base + 4U) {
        std::cerr << "RV64 framework smoke test failed\n";
        return 1;
    }
    std::cout
        << "RV64 CPU selected\n"
        << "One RV64I NOP retired; pc=0x"
        << std::hex << state.pc << std::dec << '\n';
    return 0;
}

int run_raw(
    int argc,
    char** argv,
    std::uint64_t ram_size)
{
    if (argc < 3 || argc > 4) {
        std::cerr
            << "usage: riscv_emulator --cpu rv64 --run-raw "
            << "<image.bin> [max-steps]\n";
        return 2;
    }
    std::uint64_t limit = default_step_limit;
    if (argc == 4 &&
        !parse_limit(argv[3], limit)) {
        std::cerr << "Invalid RV64 step limit\n";
        return 2;
    }

    std::vector<std::uint8_t> image;
    if (!read_image(argv[2], image)) {
        return 3;
    }
    auto machine_owner = create_machine({
        .ram_size = ram_size,
        .enable_framebuffer = false,
    }, ram_size);
    if (machine_owner == nullptr) {
        return 4;
    }
    auto& machine = *machine_owner;
    if (machine.load_image(
            image,
            platform::address_map::dram_base) != rv::BusFault::None) {
        std::cerr << "RV64 image does not fit guest RAM\n";
        return 4;
    }
    machine.reset({
        .reset_pc = platform::address_map::dram_base,
    });

    for (std::uint64_t step = 0; step < limit; ++step) {
        const StepResult result = machine.step();
        if (result.status == StepStatus::Retired) {
            continue;
        }
        if (result.status == StepStatus::TrapTaken &&
            (result.instruction == 0x00100073U ||
             result.instruction == 0x00009002U)) {
            const auto state = machine.core().snapshot();
            std::cout
                << "RV64 bare-metal program completed after "
                << state.instructions_retired
                << " retired instructions\n";
            return 0;
        }
        if (result.status == StepStatus::TrapTaken ||
            result.status == StepStatus::WaitingForInterrupt) {
            continue;
        }
        std::cerr
            << "RV64 guest stopped: status="
            << static_cast<unsigned int>(result.status)
            << ", pc=0x" << std::hex << result.pc
            << ", instruction=0x" << result.instruction
            << std::dec << '\n';
        return 5;
    }
    std::cerr << "RV64 machine-step limit reached\n";
    return 6;
}

int load_images(
    int argc,
    char** argv,
    std::uint64_t ram_size)
{
    if (argc != 5) {
        std::cerr
            << "usage: riscv_emulator --cpu rv64 --load-images "
            << "<firmware.bin> <kernel> <board.dtb>\n";
        return 2;
    }
    std::vector<std::uint8_t> firmware;
    std::vector<std::uint8_t> kernel;
    std::vector<std::uint8_t> device_tree;
    if (!read_image(argv[2], firmware) ||
        !read_image(argv[3], kernel) ||
        !read_image(argv[4], device_tree)) {
        return 3;
    }
    if (!configure_device_tree(device_tree, ram_size)) {
        return 4;
    }

    auto machine_owner = create_machine({
        .ram_size = ram_size,
    }, ram_size);
    if (machine_owner == nullptr) {
        return 4;
    }
    auto& machine = *machine_owner;
    const auto result = machine.load_boot({
        .firmware = firmware,
        .kernel = kernel,
        .device_tree = device_tree,
    });
    if (!result.ok()) {
        std::cerr
            << "Cannot prepare RV64 boot: "
            << platform::boot_error_message(result.error)
            << '\n';
        return 4;
    }
    const auto state = machine.core().snapshot();
    std::cout
        << "RV64 boot images loaded:\n"
        << "  firmware=0x" << std::hex
        << result.layout.firmware_address
        << "\n  kernel=0x" << result.layout.kernel_address
        << "\n  dtb=0x" << result.layout.device_tree_address
        << "\n  a0=0x" << state.registers[10]
        << ", a1=0x" << state.registers[11]
        << std::dec
        << "\n  RAM=" << ram_size / bytes_per_mib
        << " MiB (DTB synchronized)\n";
    return 0;
}

int run_boot(
    int argc,
    char** argv,
    bool use_virtual_disk,
    bool use_gui,
    std::uint64_t ram_size)
{
    const int required_arguments = use_virtual_disk ? 6 : 5;
    if (argc != required_arguments &&
        argc != required_arguments + 1) {
        std::cerr
            << "usage: riscv_emulator --cpu rv64 "
            << (use_virtual_disk ? "--boot-disk " : "--boot ")
            << "<opensbi.bin> <kernel> <board.dtb> ";
        if (use_virtual_disk) {
            std::cerr << "<disk-image> ";
        }
        std::cerr << "[max-steps]\n";
        return 2;
    }

    std::uint64_t step_limit =
        use_virtual_disk
            ? unlimited_step_limit
            : default_boot_step_limit;
    const int step_limit_index = use_virtual_disk ? 6 : 5;
    if (argc == required_arguments + 1 &&
        !parse_limit(argv[step_limit_index], step_limit)) {
        std::cerr << "Invalid RV64 boot step limit\n";
        return 2;
    }

    std::vector<std::uint8_t> firmware;
    std::vector<std::uint8_t> kernel;
    std::vector<std::uint8_t> device_tree;
    if (!read_image(argv[2], firmware) ||
        !read_image(argv[3], kernel) ||
        !read_image(argv[4], device_tree)) {
        return 3;
    }
    if (!configure_device_tree(device_tree, ram_size)) {
        return 4;
    }

    const char* virtual_disk_path = nullptr;
    std::shared_ptr<rv::devices::BlockStorage> virtual_disk;
    platform::MachineConfig machine_config;
    machine_config.ram_size = ram_size;
    if (use_virtual_disk) {
        virtual_disk_path = argv[5];
        try {
            virtual_disk =
                std::make_shared<rv::devices::FileBlockStorage>(
                    virtual_disk_path);
        } catch (const std::exception& error) {
            std::cerr
                << "Cannot open RV64 virtual disk: "
                << virtual_disk_path << ": "
                << error.what() << '\n';
            return 3;
        }
        if ((virtual_disk->size() %
             rv::devices::VirtioBlock::sector_size) != 0U) {
            std::cerr
                << "RV64 virtual disk size must be a multiple of "
                << rv::devices::VirtioBlock::sector_size
                << " bytes: " << virtual_disk_path << '\n';
            return 3;
        }
        machine_config.virtual_disk_storage = virtual_disk;
    }

#if defined(RV_ENABLE_NETWORK)
    rv::app::SlirpNetworkBackend network_backend;
    const bool network_diagnostics_enabled =
        std::getenv("RV_NETWORK_DIAGNOSTICS") != nullptr;
    if (!network_backend.ready()) {
        std::cerr
            << "Cannot start RV64 user-mode network: "
            << network_backend.error() << '\n';
        return 9;
    }
#endif

    auto machine_owner = create_machine(machine_config, ram_size);
    if (machine_owner == nullptr) {
        return 4;
    }
    auto& machine = *machine_owner;
#if defined(RV_ENABLE_NETWORK)
    machine.virtio_net().set_backend(&network_backend);
#endif
    const auto boot = machine.load_boot({
        .firmware = firmware,
        .kernel = kernel,
        .device_tree = device_tree,
    });
    if (!boot.ok()) {
        std::cerr
            << "Cannot prepare RV64 boot: "
            << platform::boot_error_message(boot.error)
            << '\n';
        return 4;
    }

    const auto initial = machine.core().snapshot();
    std::cout
        << "RV64 boot images loaded:\n"
        << "  OpenSBI     0x" << std::hex
        << boot.layout.firmware_address
        << " (" << std::dec << firmware.size() << " bytes)"
        << "\n  payload     0x" << std::hex
        << boot.layout.kernel_address
        << " (" << std::dec << kernel.size() << " bytes)"
        << "\n  DTB         0x" << std::hex
        << boot.layout.device_tree_address
        << " (" << std::dec << device_tree.size() << " bytes)"
        << "\nCPU ready: pc=0x" << std::hex << initial.pc
        << ", a0=0x" << initial.registers[10]
        << ", a1=0x" << initial.registers[11]
        << std::dec
        << "\n  RAM         " << ram_size / bytes_per_mib
        << " MiB (DTB synchronized)"
        << '\n';
    if (use_virtual_disk) {
        std::cout
            << "RV64 virtual disk loaded: "
            << virtual_disk_path
            << " (" << machine.virtio_block().storage_size()
            << " bytes, file-backed read-write)\n";
    }
#if defined(RV_ENABLE_NETWORK)
    std::cout
        << "RV64 user-mode network enabled: VirtIO net, "
        << "libslirp " << network_backend.version()
        << ", DHCP/DNS gateway 10.0.2.2.\n";
#endif
    std::cout << "Starting RV64 guest; machine-step limit: ";
    if (step_limit == unlimited_step_limit) {
        std::cout << "unlimited\n";
    } else {
        std::cout << step_limit << '\n';
    }
    std::cout
        << "Host terminal input is connected to RV64 guest UART.\n\n";

    const auto boot_started = std::chrono::steady_clock::now();
    const auto finish =
        [&](int result) {
            print_performance_diagnostics(
                machine,
                std::chrono::steady_clock::now() - boot_started);
#if defined(RV_ENABLE_NETWORK)
            const auto& device =
                machine.virtio_net().statistics();
            const auto& host = network_backend.statistics();
            std::cout
                << "RV64 network statistics:\n"
                << "  VirtIO TX=" << device.transmitted_frames
                << " frames/" << device.transmitted_bytes
                << " bytes, RX=" << device.received_frames
                << " frames/" << device.received_bytes
                << " bytes, drops="
                << device.dropped_transmit_frames +
                       device.dropped_receive_frames
                << ", pending="
                << device.pending_receive_frames
                << "/" << device.peak_pending_receive_frames
                << ", rx-starvations="
                << device.receive_queue_starvations
                << '\n'
                << "  libslirp guest-to-host="
                << host.guest_to_host_frames
                << ", host-to-guest="
                << host.host_to_guest_frames
                << ", queued=" << host.queued_host_frames
                << "/" << host.peak_queued_host_frames
                << ", polls=" << host.poll_calls
                << ", observed-sockets="
                << host.poll_socket_observations
                << ", ready-events="
                << host.poll_ready_events
                << ", poll-errors=" << host.poll_errors
                << ", last-poll-error="
                << host.last_poll_error
                << ", socket-add="
                << host.poll_socket_registrations
                << ", socket-remove="
                << host.poll_socket_unregistrations
                << ", errors=" << host.guest_errors
                << '\n';
#endif
            return synchronize_disk(machine, virtual_disk_path)
                       ? result
                       : 7;
        };

#if defined(RV_ENABLE_SDL)
    std::unique_ptr<rv::app::SdlFrontend> gui;
    if (use_gui) {
        gui = std::make_unique<rv::app::SdlFrontend>();
        if (!gui->ready()) {
            std::cerr
                << "Cannot start RV64 SDL graphical frontend: "
                << gui->error() << '\n';
            return finish(8);
        }
        gui->present(machine.framebuffer());
        std::cout
            << "RV64 SDL window enabled: F1=UART terminal, "
            << "F2=framebuffer, drag=select, "
            << "right-click=copy/paste, "
            << "Ctrl+Shift+C/V=copy/paste.\n";
    }
#else
    if (use_gui) {
        std::cerr
            << "This build does not include SDL graphical support.\n";
        return finish(8);
    }
#endif

    for (std::uint64_t step = 0; step < step_limit; ++step) {
#if defined(RV_ENABLE_NETWORK)
        if (network_diagnostics_enabled && step != 0U &&
            (step % network_diagnostic_interval) == 0U) {
            const auto& device = machine.virtio_net().statistics();
            const auto& host = network_backend.statistics();
            std::cerr
                << "\nRV64 network live: step=" << step
                << ", TX=" << device.transmitted_frames
                << ", RX=" << device.received_frames
                << ", drops="
                << device.dropped_transmit_frames +
                       device.dropped_receive_frames
                << ", pending="
                << device.pending_receive_frames
                << "/" << device.peak_pending_receive_frames
                << ", rx-starvations="
                << device.receive_queue_starvations
                << ", dma-failures=" << device.dma_failures
                << ", slirp-queue=" << host.queued_host_frames
                << "/" << host.peak_queued_host_frames
                << ", slirp-drops=" << host.dropped_host_frames
                << ", poll-errors=" << host.poll_errors
                << ", last-poll-error=" << host.last_poll_error
                << '\n'
                << std::flush;
        }
#endif
        if ((step % console_poll_interval) == 0U) {
            forward_console_input(machine);
#if defined(RV_ENABLE_SDL)
            if (gui != nullptr && gui->active()) {
                const auto input = gui->poll_input();
                if (!input.empty()) {
                    machine.uart().inject_received(input);
                }
                gui->present(machine.framebuffer());
            }
#endif
        }
        const StepResult result = machine.step();
        const std::string output = machine.uart().take_transmitted();
        if (!output.empty()) {
            std::cout << output << std::flush;
        }
#if defined(RV_ENABLE_SDL)
        if (gui != nullptr && !output.empty()) {
            gui->append_uart(output);
        }
#endif

        const auto action = machine.syscon().requested_action();
        if (action != rv::devices::SystemAction::None) {
            const char* action_name =
                action == rv::devices::SystemAction::PowerOff
                    ? "power off"
                    : "reboot";
            std::cout
                << "\nRV64 guest requested " << action_name
                << " after " << step + 1U
                << " machine steps.\n";
            return finish(0);
        }

        if (result.status != StepStatus::Retired &&
            result.status != StepStatus::TrapTaken &&
            result.status != StepStatus::WaitingForInterrupt) {
            std::cerr
                << "\nRV64 guest stopped: status="
                << static_cast<unsigned int>(result.status)
                << ", pc=0x" << std::hex << result.pc
                << ", instruction=0x" << result.instruction
                << std::dec << '\n';
            return finish(5);
        }
    }

    const auto state = machine.core().snapshot();
    std::cerr
        << "\nRV64 boot step limit reached; retired="
        << state.instructions_retired
        << ", pc=0x" << std::hex << state.pc
        << std::dec << '\n';
    return finish(6);
}

} // namespace

int run_cli(int argc, char** argv)
{
    std::uint64_t ram_size = platform::address_map::default_dram_size;
    if (argc >= 2 &&
        std::string_view(argv[1]) == "--ram-mib") {
        if (argc < 3 || !parse_ram_size(argv[2], ram_size)) {
            std::cerr
                << "RV64 --ram-mib requires an integer from "
                << minimum_ram_mib << " through "
                << maximum_ram_mib << '\n';
            return 2;
        }
        argc -= 2;
        argv += 2;
    }
    if (argc == 1) {
        return run_smoke();
    }
    const std::string_view command = argv[1];
    if (command == "--run-raw") {
        return run_raw(argc, argv, ram_size);
    }
    if (command == "--load-images") {
        return load_images(argc, argv, ram_size);
    }
    if (command == "--boot") {
        return run_boot(argc, argv, false, false, ram_size);
    }
    if (command == "--boot-disk") {
        return run_boot(argc, argv, true, false, ram_size);
    }
    if (command == "--gui") {
        if (argc >= 3 &&
            std::string_view(argv[2]) == "--boot") {
            return run_boot(
                argc - 1,
                argv + 1,
                false,
                true,
                ram_size);
        }
        if (argc >= 3 &&
            std::string_view(argv[2]) == "--boot-disk") {
            return run_boot(
                argc - 1,
                argv + 1,
                true,
                true,
                ram_size);
        }
        std::cerr
            << "RV64 --gui must be followed by "
            << "--boot or --boot-disk\n";
        return 2;
    }
    std::cerr
        << "usage:\n"
        << "  riscv_emulator --cpu rv64\n"
        << "  riscv_emulator --cpu rv64 [--ram-mib <MiB>] "
        << "--run-raw "
        << "<image.bin> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 [--ram-mib <MiB>] "
        << "--load-images "
        << "<firmware.bin> <kernel> <board.dtb>\n"
        << "  riscv_emulator --cpu rv64 [--ram-mib <MiB>] --boot "
        << "<opensbi.bin> <kernel> <board.dtb> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 [--ram-mib <MiB>] "
        << "--boot-disk "
        << "<opensbi.bin> <kernel> <board.dtb> "
        << "<disk-image> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 [--ram-mib <MiB>] "
        << "--gui --boot "
        << "<opensbi.bin> <kernel> <board.dtb> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 [--ram-mib <MiB>] "
        << "--gui --boot-disk "
        << "<opensbi.bin> <kernel> <board.dtb> "
        << "<disk-image> [max-steps]\n";
    return 2;
}

} // namespace rv64::app
