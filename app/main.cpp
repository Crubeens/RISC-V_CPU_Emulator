#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#include "rv32/core/core.hpp"
#include "rv32/devices/clint.hpp"
#include "rv32/devices/syscon.hpp"
#include "rv32/devices/uart16550.hpp"
#include "rv32/devices/virtio_block.hpp"
#include "rv32/platform/machine.hpp"

namespace {

constexpr std::uint64_t default_step_limit = 100'000'000ULL;
constexpr std::uint64_t unlimited_step_limit =
    std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t console_poll_interval = 1024ULL;

struct OwnedBootImages {
    std::vector<std::uint8_t> firmware;
    std::vector<std::uint8_t> kernel;
    std::vector<std::uint8_t> device_tree;
    std::vector<std::uint8_t> virtual_disk;
};

bool read_binary_image(
    const char* path,
    std::string_view image_name,
    std::vector<std::uint8_t>& image)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr
            << "Cannot open " << image_name << ": "
            << path << '\n';
        return false;
    }

    image.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (input.bad()) {
        std::cerr
            << "Cannot read " << image_name << ": "
            << path << '\n';
        return false;
    }
    if (image.empty()) {
        std::cerr
            << image_name << " is empty: "
            << path << '\n';
        return false;
    }
    return true;
}

bool read_boot_images(char** argv, OwnedBootImages& images)
{
    return
        read_binary_image(
            argv[2],
            "firmware image",
            images.firmware) &&
        read_binary_image(
            argv[3],
            "Linux kernel image",
            images.kernel) &&
        read_binary_image(
            argv[4],
            "device tree image",
            images.device_tree);
}

bool read_virtual_disk(
    const char* path,
    std::vector<std::uint8_t>& image)
{
    if (!read_binary_image(path, "virtual disk image", image)) {
        return false;
    }

    const auto sector_size = static_cast<std::size_t>(
        rv32::devices::VirtioBlock::sector_size);
    if ((image.size() % sector_size) != 0U) {
        std::cerr
            << "Virtual disk image size must be a multiple of "
            << sector_size << " bytes: "
            << path << '\n';
        return false;
    }
    return true;
}

bool write_virtual_disk(
    const char* path,
    std::span<const std::uint8_t> image)
{
    if (image.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) {
        std::cerr
            << "Virtual disk image is too large to write: "
            << path << '\n';
        return false;
    }

    std::fstream output(
        path,
        std::ios::binary |
            std::ios::in |
            std::ios::out);
    if (!output) {
        std::cerr
            << "Cannot open virtual disk image for writeback: "
            << path << '\n';
        return false;
    }

    output.write(
        reinterpret_cast<const char*>(image.data()),
        static_cast<std::streamsize>(image.size()));
    output.flush();
    if (!output) {
        std::cerr
            << "Cannot write virtual disk image: "
            << path << '\n';
        return false;
    }
    return true;
}

bool load_virtual_disk(
    rv32::platform::Machine& machine,
    const std::vector<std::uint8_t>& image)
{
    auto target = machine.virtio_block().disk_image();
    if (target.size() != image.size()) {
        std::cerr
            << "Virtual disk capacity does not match the image size\n";
        return false;
    }

    std::copy(image.begin(), image.end(), target.begin());
    machine.virtio_block().clear_dirty();
    return true;
}

bool write_back_virtual_disk(
    rv32::platform::Machine& machine,
    const char* path)
{
    auto& disk = machine.virtio_block();
    if (path == nullptr || !disk.dirty()) {
        return true;
    }
    if (!write_virtual_disk(path, disk.disk_image())) {
        return false;
    }

    disk.clear_dirty();
    std::cout
        << "Virtual disk writeback completed: "
        << path << '\n';
    return true;
}

void print_address(
    std::string_view name,
    rv32::PhysAddr address,
    std::size_t size)
{
    std::cout
        << "  " << std::left << std::setw(12) << name
        << " 0x" << std::right << std::hex << std::setw(8)
        << std::setfill('0') << address
        << std::setfill(' ') << std::dec
        << " (" << size << " bytes)\n";
}

rv32::platform::BootResult prepare_boot(
    rv32::platform::Machine& machine,
    const OwnedBootImages& images)
{
    return machine.load_boot({
        .firmware = images.firmware,
        .kernel = images.kernel,
        .device_tree = images.device_tree,
        .hart_id = 0,
    });
}

bool report_boot_error(const rv32::platform::BootResult& result)
{
    if (result.ok()) {
        return false;
    }

    std::cerr
        << "Cannot prepare boot: "
        << rv32::platform::boot_error_message(result.error)
        << '\n';
    return true;
}

void print_boot_state(
    const rv32::platform::Machine& machine,
    const OwnedBootImages& images,
    const rv32::platform::BootResult& result)
{
    std::cout << "Boot images loaded:\n";
    print_address(
        "OpenSBI",
        result.layout.firmware_address,
        images.firmware.size());
    print_address(
        "Linux",
        result.layout.kernel_address,
        images.kernel.size());
    print_address(
        "DTB",
        result.layout.device_tree_address,
        images.device_tree.size());

    const auto state = machine.core().snapshot();
    std::cout
        << "CPU ready: pc=0x" << std::hex << state.pc
        << ", a0=0x" << state.registers[10]
        << ", a1=0x" << state.registers[11]
        << std::dec << '\n';
}

int load_boot_images(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr
            << "usage: rv32_emulator --load-images "
            << "<opensbi.bin> <linux-image> <board.dtb>\n";
        return 2;
    }

    OwnedBootImages images;
    if (!read_boot_images(argv, images)) {
        return 3;
    }

    rv32::platform::Machine machine;
    const auto boot_result = prepare_boot(machine, images);
    if (report_boot_error(boot_result)) {
        return 4;
    }

    print_boot_state(machine, images, boot_result);
    return 0;
}

bool parse_step_limit(
    std::string_view text,
    std::uint64_t& step_limit)
{
    if (text.empty()) {
        return false;
    }

    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto parsed =
        std::from_chars(first, last, step_limit);
    return parsed.ec == std::errc{} &&
           parsed.ptr == last &&
           step_limit != 0;
}

void flush_uart(rv32::platform::Machine& machine)
{
    const auto output = machine.uart().take_transmitted();
    if (!output.empty()) {
        std::cout << output << std::flush;
    }
}

void forward_console_input(rv32::platform::Machine& machine)
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

bool can_continue(rv32::StepStatus status)
{
    return status == rv32::StepStatus::Retired ||
           status == rv32::StepStatus::TrapTaken ||
           status == rv32::StepStatus::WaitingForInterrupt;
}

[[nodiscard]] std::string_view privilege_name(
    rv32::PrivilegeMode privilege) noexcept
{
    switch (privilege) {
    case rv32::PrivilegeMode::User:
        return "U";
    case rv32::PrivilegeMode::Supervisor:
        return "S";
    case rv32::PrivilegeMode::Machine:
        return "M";
    }
    return "?";
}

void print_cpu_snapshot(
    std::string_view heading,
    const rv32::CpuSnapshot& state,
    std::uint64_t mtime)
{
    constexpr std::array<std::string_view, 32> register_names{
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
    };

    const std::uint32_t pending_interrupts =
        state.machine_csrs.mip_software |
        state.machine_csrs.mip_lines;

    std::cerr
        << heading << ":\n"
        << "  privilege=" << privilege_name(state.privilege)
        << ", cycle=" << state.cycle
        << ", mtime=" << mtime
        << ", retired=" << state.instructions_retired
        << '\n';

    for (std::size_t index = 0;
         index < state.registers.size();
         ++index) {
        if ((index % 4U) == 0U) {
            std::cerr << "  ";
        }
        std::cerr
            << std::left << std::setw(4) << register_names[index]
            << "=0x" << std::right << std::hex << std::setw(8)
            << std::setfill('0') << state.registers[index]
            << std::setfill(' ');
        if ((index % 4U) == 3U) {
            std::cerr << '\n';
        } else {
            std::cerr << "  ";
        }
    }

    std::cerr
        << "  mstatus=0x" << std::hex << std::setw(8)
        << std::setfill('0') << state.machine_csrs.mstatus
        << ", mie=0x" << std::setw(8) << state.machine_csrs.mie
        << ", mip=0x" << std::setw(8) << pending_interrupts
        << ", mtvec=0x" << std::setw(8)
        << state.machine_csrs.mtvec << '\n'
        << "  mepc=0x" << std::setw(8)
        << state.machine_csrs.mepc
        << ", mcause=0x" << std::setw(8)
        << state.machine_csrs.mcause
        << ", mtval=0x" << std::setw(8)
        << state.machine_csrs.mtval
        << ", medeleg=0x" << std::setw(8)
        << state.machine_csrs.medeleg << '\n'
        << "  mideleg=0x" << std::setw(8)
        << state.machine_csrs.mideleg
        << ", stvec=0x" << std::setw(8)
        << state.supervisor_csrs.stvec
        << ", sepc=0x" << std::setw(8)
        << state.supervisor_csrs.sepc
        << ", scause=0x" << std::setw(8)
        << state.supervisor_csrs.scause << '\n'
        << "  stval=0x" << std::setw(8)
        << state.supervisor_csrs.stval
        << ", satp=0x" << std::setw(8)
        << state.supervisor_csrs.satp
        << std::setfill(' ') << std::dec << '\n';
}

void print_cpu_diagnostics(rv32::platform::Machine& machine)
{
    print_cpu_snapshot(
        "CPU diagnostic snapshot",
        machine.core().snapshot(),
        machine.clint().mtime());

    const auto& disk = machine.virtio_block();
    const auto& statistics = disk.statistics();
    const auto queue = disk.queue_state();
    std::cerr
        << "VirtIO block diagnostics:\n"
        << "  notify-writes="
        << statistics.queue_notify_writes
        << ", accepted="
        << statistics.queue_notifications
        << ", rejected="
        << statistics.rejected_notifications
        << ", chains=" << statistics.descriptor_chains
        << ", completed=" << statistics.completed_requests
        << ", failed=" << statistics.failed_requests
        << '\n'
        << "  reads=" << statistics.read_requests
        << ", writes=" << statistics.write_requests
        << ", bytes=" << statistics.bytes_transferred
        << ", irq-raised=" << statistics.interrupts_raised
        << ", irq-acks="
        << statistics.interrupt_acknowledgements
        << ", dirty=" << (disk.dirty() ? "yes" : "no")
        << '\n'
        << "  queue: page-size=" << queue.page_size
        << ", selected=" << queue.selected_queue
        << ", size=" << queue.queue_size
        << ", align=" << queue.alignment
        << ", pfn=0x" << std::hex
        << queue.page_frame_number
        << ", status=0x"
        << static_cast<unsigned int>(queue.device_status)
        << std::dec
        << ", configured="
        << (queue.configured ? "yes" : "no")
        << '\n';
}

int run_boot(
    int argc,
    char** argv,
    bool use_virtual_disk)
{
    const int required_arguments = use_virtual_disk ? 6 : 5;
    if (argc != required_arguments &&
        argc != required_arguments + 1) {
        std::cerr << "usage: rv32_emulator "
            << (use_virtual_disk ? "--boot-disk " : "--boot ")
            << "<opensbi.bin> <linux-image> <board.dtb> ";
        if (use_virtual_disk) {
            std::cerr << "<disk-image> ";
        }
        std::cerr << "[max-steps]\n";
        return 2;
    }

    std::uint64_t step_limit =
        use_virtual_disk
            ? unlimited_step_limit
            : default_step_limit;
    const int step_limit_index = use_virtual_disk ? 6 : 5;
    if (argc == required_arguments + 1 &&
        !parse_step_limit(
            argv[step_limit_index],
            step_limit)) {
        std::cerr
            << "max-steps must be a positive decimal integer\n";
        return 2;
    }

    OwnedBootImages images;
    if (!read_boot_images(argv, images)) {
        return 3;
    }

    const char* virtual_disk_path = nullptr;
    rv32::platform::MachineConfig machine_config;
    if (use_virtual_disk) {
        virtual_disk_path = argv[5];
        if (!read_virtual_disk(
                virtual_disk_path,
                images.virtual_disk)) {
            return 3;
        }
        machine_config.virtual_disk_size =
            static_cast<std::uint64_t>(
                images.virtual_disk.size());
    }

    rv32::platform::Machine machine(machine_config);
    if (use_virtual_disk &&
        !load_virtual_disk(
            machine,
            images.virtual_disk)) {
        return 3;
    }

    const auto boot_result = prepare_boot(machine, images);
    if (report_boot_error(boot_result)) {
        return 4;
    }
    print_boot_state(machine, images, boot_result);
    if (use_virtual_disk) {
        std::cout
            << "Virtual disk loaded: "
            << virtual_disk_path
            << " (" << images.virtual_disk.size()
            << " bytes, read-write)\n";
    }
    std::cout << "Starting guest; machine-step limit: ";
    if (step_limit == unlimited_step_limit) {
        std::cout << "unlimited\n";
    } else {
        std::cout << step_limit << '\n';
    }
    std::cout
        << "Host terminal input is connected to guest UART.\n";

    const auto finish =
        [&machine, virtual_disk_path](int result) {
            return write_back_virtual_disk(
                       machine,
                       virtual_disk_path)
                       ? result
                       : 7;
        };

    for (std::uint64_t step = 0; step < step_limit; ++step) {
        if ((step % console_poll_interval) == 0U) {
            forward_console_input(machine);
        }
        const auto result = machine.step();
        flush_uart(machine);

        const auto action = machine.syscon().requested_action();
        if (action != rv32::devices::SystemAction::None) {
            const auto action_name =
                action == rv32::devices::SystemAction::PowerOff
                    ? "power off"
                    : "reboot";
            std::cout
                << "\nGuest requested " << action_name
                << " after " << step + 1U
                << " machine steps.\n";
            return finish(0);
        }

        if (!can_continue(result.status)) {
            std::cerr
                << "\nCPU stopped with status "
                << static_cast<unsigned int>(result.status)
                << " at pc=0x" << std::hex << result.pc
                << ", instruction=0x" << result.instruction
                << std::dec << '\n';
            print_cpu_diagnostics(machine);
            return finish(5);
        }
    }

    flush_uart(machine);
    const auto state = machine.core().snapshot();
    std::cerr
        << "\nMachine-step limit reached; retired="
        << state.instructions_retired
        << ", pc=0x" << std::hex << state.pc
        << std::dec << '\n';
    print_cpu_diagnostics(machine);
    return finish(6);
}

int run_framework_smoke()
{
    rv32::platform::Machine machine;
    const auto reset_snapshot = machine.core().snapshot();

    std::cout
        << "RISC-V32 CPU Emulator framework v0.1.0\n"
        << "CPU profile: " << rv32::Core::isa_string() << "\n"
        << "Mapped devices:\n";

    for (const auto& device : machine.device_map()) {
        std::cout
            << "  " << std::left << std::setw(22) << device.name
            << " [0x" << std::right << std::hex << std::setw(8)
            << std::setfill('0') << device.range.base
            << ", 0x" << std::setw(8)
            << device.range.end_exclusive() << ")\n"
            << std::setfill(' ') << std::dec;
    }

    constexpr std::uint32_t nop = 0x00000013U;
    const auto load_fault = machine.bus().dma_write(
        reset_snapshot.pc,
        rv32::AccessWidth::Word,
        nop);
    if (load_fault != rv32::BusFault::None) {
        std::cerr << "Failed to load the framework NOP instruction\n";
        return 1;
    }

    const auto step_result = machine.step();
    const auto snapshot = machine.core().snapshot();
    const bool retired =
        step_result.status == rv32::StepStatus::Retired;

    std::cout
        << "Reset PC: 0x" << std::hex
        << reset_snapshot.pc << "\n"
        << "Current PC: 0x" << snapshot.pc << std::dec << "\n"
        << "Framework step status: "
        << (retired
                ? "one RV32I NOP retired"
                : "unexpected")
        << "\n"
        << "Framework assembly completed successfully.\n";

    return retired ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 1) {
        return run_framework_smoke();
    }
    if (std::string_view(argv[1]) == "--load-images") {
        return load_boot_images(argc, argv);
    }
    if (std::string_view(argv[1]) == "--boot") {
        return run_boot(argc, argv, false);
    }
    if (std::string_view(argv[1]) == "--boot-disk") {
        return run_boot(argc, argv, true);
    }

    std::cerr
        << "usage:\n"
        << "  rv32_emulator\n"
        << "  rv32_emulator --load-images "
        << "<opensbi.bin> <linux-image> <board.dtb>\n"
        << "  rv32_emulator --boot "
        << "<opensbi.bin> <linux-image> <board.dtb> "
        << "[max-steps]\n"
        << "  rv32_emulator --boot-disk "
        << "<opensbi.bin> <linux-image> <board.dtb> "
        << "<disk-image> "
        << "[max-steps]\n";
    return 2;
}
