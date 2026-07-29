#include "rv/devices/virtio_block.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

#include "rv/platform/endian.hpp"

namespace rv::devices {

namespace {

[[nodiscard]] constexpr std::uint64_t align_up(
    std::uint64_t value,
    std::uint64_t alignment) noexcept
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] constexpr bool is_power_of_two(
    std::uint64_t value) noexcept
{
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] std::uint64_t rounded_disk_size(
    std::uint64_t requested)
{
    const auto minimum =
        requested == 0 ? VirtioBlock::sector_size : requested;
    const auto sector = static_cast<std::uint64_t>(
        VirtioBlock::sector_size);
    if (minimum >
        std::numeric_limits<std::uint64_t>::max() - (sector - 1U)) {
        throw std::invalid_argument("virtual disk size is too large");
    }
    return ((minimum + sector - 1U) / sector) * sector;
}

} // namespace

VirtioBlock::VirtioBlock(
    PhysAddr base,
    std::uint64_t size,
    std::vector<std::uint8_t> disk_image)
    : range_{.base = base, .size = size}
{
    const auto target_size =
        rounded_disk_size(static_cast<std::uint64_t>(disk_image.size()));
    if (target_size >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("virtual disk image is too large");
    }
    disk_image.resize(static_cast<std::size_t>(target_size));
    auto storage =
        std::make_shared<MemoryBlockStorage>(std::move(disk_image));
    memory_storage_ = storage.get();
    storage_ = std::move(storage);
    reset();
}

VirtioBlock::VirtioBlock(
    PhysAddr base,
    std::uint64_t size,
    std::uint64_t disk_size)
    : range_{.base = base, .size = size}
{
    const auto target_size = rounded_disk_size(disk_size);
    auto storage = std::make_shared<MemoryBlockStorage>(target_size);
    memory_storage_ = storage.get();
    storage_ = std::move(storage);
    reset();
}

VirtioBlock::VirtioBlock(
    PhysAddr base,
    std::uint64_t size,
    std::shared_ptr<BlockStorage> storage)
    : range_{.base = base, .size = size},
      storage_(std::move(storage))
{
    if (storage_ == nullptr ||
        storage_->size() == 0U ||
        (storage_->size() % sector_size) != 0U) {
        throw std::invalid_argument(
            "virtual disk storage must contain whole sectors");
    }
    reset();
}

std::string_view VirtioBlock::name() const noexcept
{
    return "VirtIO MMIO Block";
}

platform::AddressRange VirtioBlock::range() const noexcept
{
    return range_;
}

ReadResult VirtioBlock::read(
    std::uint64_t offset,
    AccessWidth width)
{
    if (offset >= config_offset) {
        std::array<std::uint8_t, sizeof(std::uint64_t)> capacity{};
        const auto sectors =
            storage_->size() /
            static_cast<std::uint64_t>(sector_size);
        const auto fault = platform::write_little_endian(
            capacity,
            0,
            AccessWidth::DoubleWord,
            sectors);
        if (fault != BusFault::None) {
            return {.fault = fault};
        }
        return platform::read_little_endian(
            capacity,
            offset - config_offset,
            width);
    }

    if (width != AccessWidth::Word || (offset & 0x3U) != 0) {
        return {.fault = BusFault::Unsupported};
    }

    std::uint32_t value = 0;
    switch (offset) {
    case magic_value_offset:
        value = magic_value;
        break;
    case version_offset:
        value = legacy_version;
        break;
    case device_id_offset:
        value = block_device_id;
        break;
    case vendor_id_offset:
        value = vendor_id;
        break;
    case device_features_offset:
        value = 0;
        break;
    case queue_num_max_offset:
        value = queue_select_ == 0 ? maximum_queue_size : 0;
        break;
    case queue_pfn_offset:
        value = queue_pfn_;
        break;
    case interrupt_status_offset:
        value = interrupt_status_;
        break;
    case status_offset:
        value = device_status_;
        break;
    default:
        value = 0;
        break;
    }

    return {
        .fault = BusFault::None,
        .value = value,
    };
}

BusFault VirtioBlock::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    if (offset >= config_offset) {
        return BusFault::ReadOnly;
    }
    if (width != AccessWidth::Word || (offset & 0x3U) != 0) {
        return BusFault::Unsupported;
    }

    const auto word = static_cast<std::uint32_t>(value);
    switch (offset) {
    case device_features_select_offset:
        device_features_select_ = word;
        break;
    case driver_features_offset:
        if (driver_features_select_ == 0) {
            driver_features_ = word;
        }
        break;
    case driver_features_select_offset:
        driver_features_select_ = word;
        break;
    case guest_page_size_offset:
        guest_page_size_ = word;
        update_queue_addresses();
        break;
    case queue_select_offset:
        queue_select_ = word;
        update_queue_addresses();
        break;
    case queue_num_offset:
        queue_num_ = static_cast<std::uint16_t>(word);
        update_queue_addresses();
        break;
    case queue_align_offset:
        queue_align_ = word;
        update_queue_addresses();
        break;
    case queue_pfn_offset:
        queue_pfn_ = word;
        update_queue_addresses();
        break;
    case queue_notify_offset:
        ++statistics_.queue_notify_writes;
        if (word == 0 && queue_configured()) {
            notification_pending_ = true;
            ++statistics_.queue_notifications;
        } else {
            ++statistics_.rejected_notifications;
        }
        break;
    case interrupt_ack_offset:
        if ((word & 0x1U) != 0U) {
            ++statistics_.interrupt_acknowledgements;
        }
        interrupt_status_ &=
            static_cast<std::uint8_t>(~word);
        break;
    case status_offset:
        if (word == 0) {
            // Linux writes the legacy MMIO guest page size before
            // register_virtio_device() performs a device-status reset.
            // Retaining that transport value across status reset matches
            // established legacy MMIO behavior while queues are cleared.
            const auto guest_page_size = guest_page_size_;
            reset();
            guest_page_size_ = guest_page_size;
        } else {
            device_status_ = static_cast<std::uint8_t>(word);
        }
        break;
    default:
        break;
    }

    return BusFault::None;
}

void VirtioBlock::tick(
    platform::DmaAccess& dma,
    std::uint64_t cycles)
{
    static_cast<void>(cycles);

    if (!notification_pending_) {
        return;
    }

    notification_pending_ = false;
    process_queue(dma);
}

bool VirtioBlock::interrupt_pending() const noexcept
{
    return (interrupt_status_ & 0x1U) != 0;
}

bool VirtioBlock::dirty() const noexcept
{
    return disk_dirty_;
}

void VirtioBlock::clear_dirty() noexcept
{
    disk_dirty_ = false;
}

const VirtioBlockStatistics& VirtioBlock::statistics()
    const noexcept
{
    return statistics_;
}

VirtioBlockQueueState VirtioBlock::queue_state() const noexcept
{
    return {
        .page_size = guest_page_size_,
        .selected_queue = queue_select_,
        .queue_size = queue_num_,
        .alignment = queue_align_,
        .page_frame_number = queue_pfn_,
        .device_status = device_status_,
        .configured = queue_configured(),
    };
}

std::span<std::uint8_t> VirtioBlock::disk_image() noexcept
{
    return memory_storage_ == nullptr
               ? std::span<std::uint8_t>{}
               : memory_storage_->bytes();
}

std::span<const std::uint8_t> VirtioBlock::disk_image() const noexcept
{
    return memory_storage_ == nullptr
               ? std::span<const std::uint8_t>{}
               : memory_storage_->bytes();
}

std::uint64_t VirtioBlock::storage_size() const noexcept
{
    return storage_->size();
}

bool VirtioBlock::file_backed() const noexcept
{
    return storage_->file_backed();
}

bool VirtioBlock::flush() noexcept
{
    return storage_->flush();
}

void VirtioBlock::reset() noexcept
{
    device_features_select_ = 0;
    driver_features_select_ = 0;
    driver_features_ = 0;
    guest_page_size_ = 0;
    queue_select_ = 0;
    queue_num_ = 0;
    queue_align_ = 4096;
    queue_pfn_ = 0;
    interrupt_status_ = 0;
    device_status_ = 0;
    descriptor_table_ = 0;
    available_ring_ = 0;
    used_ring_ = 0;
    last_available_index_ = 0;
    used_index_ = 0;
    notification_pending_ = false;
}

void VirtioBlock::update_queue_addresses() noexcept
{
    if (!queue_configured()) {
        descriptor_table_ = 0;
        available_ring_ = 0;
        used_ring_ = 0;
        return;
    }

    descriptor_table_ =
        static_cast<PhysAddr>(queue_pfn_) *
        static_cast<std::uint64_t>(guest_page_size_);
    available_ring_ =
        descriptor_table_ +
        static_cast<std::uint64_t>(queue_num_) * 16U;

    const auto available_size =
        4U + static_cast<std::uint64_t>(queue_num_) * 2U;
    used_ring_ = align_up(
        available_ring_ + available_size,
        queue_align_);
}

void VirtioBlock::process_queue(platform::DmaAccess& dma)
{
    if (!queue_configured()) {
        return;
    }

    const auto available_index_result = dma_read_value(
        dma,
        available_ring_ + 2U,
        AccessWidth::HalfWord);
    if (!available_index_result.ok()) {
        return;
    }

    const auto available_index =
        static_cast<std::uint16_t>(available_index_result.value);
    std::uint16_t processed = 0;

    while (last_available_index_ != available_index &&
           processed < queue_num_) {
        const auto ring_entry =
            available_ring_ + 4U +
            static_cast<std::uint64_t>(
                last_available_index_ % queue_num_) *
                sizeof(std::uint16_t);
        const auto head_result =
            dma_read_value(dma, ring_entry, AccessWidth::HalfWord);
        if (!head_result.ok()) {
            break;
        }

        const auto head =
            static_cast<std::uint16_t>(head_result.value);
        std::uint32_t bytes_written = 0;
        ++statistics_.descriptor_chains;
        static_cast<void>(
            process_request(dma, head, bytes_written));

        const auto used_element =
            used_ring_ + 4U +
            static_cast<std::uint64_t>(
                used_index_ % queue_num_) *
                8U;

        if (!dma_write_value(
                dma,
                used_element,
                AccessWidth::Word,
                head) ||
            !dma_write_value(
                dma,
                used_element + 4U,
                AccessWidth::Word,
                bytes_written)) {
            break;
        }

        ++used_index_;
        if (!dma_write_value(
                dma,
                used_ring_ + 2U,
                AccessWidth::HalfWord,
                used_index_)) {
            break;
        }

        ++last_available_index_;
        ++processed;
        interrupt_status_ |= 0x1U;
        ++statistics_.interrupts_raised;
    }
}

bool VirtioBlock::process_request(
    platform::DmaAccess& dma,
    std::uint16_t head,
    std::uint32_t& bytes_written)
{
    bytes_written = 0;
    if (head >= queue_num_) {
        return false;
    }

    std::vector<Descriptor> chain;
    chain.reserve(queue_num_);
    std::vector<bool> visited(queue_num_, false);

    auto current = head;
    for (std::uint16_t count = 0;
         count < queue_num_;
         ++count) {
        if (current >= queue_num_ ||
            visited[static_cast<std::size_t>(current)]) {
            return false;
        }

        visited[static_cast<std::size_t>(current)] = true;
        Descriptor descriptor;
        if (!load_descriptor(dma, current, descriptor)) {
            return false;
        }
        chain.push_back(descriptor);

        if ((descriptor.flags & descriptor_has_next) == 0) {
            break;
        }
        current = descriptor.next;
    }

    if (chain.size() < 2) {
        return false;
    }

    const auto& header = chain.front();
    const auto& status_descriptor = chain.back();
    if (header.length < 16U ||
        status_descriptor.length < 1U ||
        (status_descriptor.flags & descriptor_is_write) == 0) {
        return false;
    }

    const auto type_result = dma_read_value(
        dma,
        header.address,
        AccessWidth::Word);
    const auto sector_result = dma_read_value(
        dma,
        header.address + 8U,
        AccessWidth::DoubleWord);
    if (!type_result.ok() || !sector_result.ok()) {
        return false;
    }

    const auto request_type =
        static_cast<std::uint32_t>(type_result.value);
    const auto sector = sector_result.value;
    if (request_type == request_read) {
        ++statistics_.read_requests;
    } else if (request_type == request_write) {
        ++statistics_.write_requests;
    }

    std::uint8_t completion_status = status_ok;
    std::uint64_t disk_offset = 0;
    if (sector >
        std::numeric_limits<std::uint64_t>::max() /
            static_cast<std::uint64_t>(sector_size)) {
        completion_status = status_io_error;
    } else {
        disk_offset =
            sector * static_cast<std::uint64_t>(sector_size);
    }

    if (request_type != request_read &&
        request_type != request_write) {
        completion_status = status_unsupported;
    }

    for (std::size_t descriptor_index = 1;
         descriptor_index + 1 < chain.size() &&
         completion_status == status_ok;
         ++descriptor_index) {
        const auto& data_descriptor = chain[descriptor_index];
        const auto data_length =
            static_cast<std::uint64_t>(data_descriptor.length);
        const auto disk_size = storage_->size();

        if (data_length > disk_size ||
            disk_offset > disk_size - data_length) {
            completion_status = status_io_error;
            break;
        }

        const bool guest_writable =
            (data_descriptor.flags & descriptor_is_write) != 0;
        if ((request_type == request_read && !guest_writable) ||
            (request_type == request_write && guest_writable)) {
            completion_status = status_io_error;
            break;
        }

        std::array<std::uint8_t, 4096> transfer_buffer{};
        std::uint64_t transferred = 0;
        while (transferred < data_length) {
            const auto remaining = data_length - transferred;
            const auto chunk_size = static_cast<std::size_t>(
                std::min<std::uint64_t>(
                    remaining,
                    transfer_buffer.size()));
            auto chunk = std::span(transfer_buffer).first(chunk_size);
            if (request_type == request_read) {
                if (!storage_->read(
                        disk_offset + transferred,
                        chunk)) {
                    completion_status = status_io_error;
                    break;
                }
            } else {
                for (std::size_t byte = 0;
                     byte < chunk_size;
                     ++byte) {
                    const auto byte_result = dma_read_value(
                        dma,
                        data_descriptor.address + transferred + byte,
                        AccessWidth::Byte);
                    if (!byte_result.ok()) {
                        completion_status = status_io_error;
                        break;
                    }
                    chunk[byte] =
                        static_cast<std::uint8_t>(byte_result.value);
                }
                if (completion_status != status_ok ||
                    !storage_->write(
                        disk_offset + transferred,
                        chunk)) {
                    completion_status = status_io_error;
                    break;
                }
                disk_dirty_ = true;
            }
            if (request_type == request_read) {
                for (std::size_t byte = 0;
                     byte < chunk_size;
                     ++byte) {
                    if (!dma_write_value(
                            dma,
                            data_descriptor.address + transferred + byte,
                            AccessWidth::Byte,
                            chunk[byte])) {
                        completion_status = status_io_error;
                        break;
                    }
                    ++bytes_written;
                }
                if (completion_status != status_ok) {
                    break;
                }
            }
            statistics_.bytes_transferred += chunk_size;
            transferred += chunk_size;
        }

        disk_offset += data_length;
    }

    const bool status_written = dma_write_value(
        dma,
        status_descriptor.address,
        AccessWidth::Byte,
        completion_status);
    if (completion_status != status_ok || !status_written) {
        ++statistics_.failed_requests;
    }
    if (status_written) {
        ++bytes_written;
        ++statistics_.completed_requests;
    }
    return status_written;
}

bool VirtioBlock::load_descriptor(
    platform::DmaAccess& dma,
    std::uint16_t index,
    Descriptor& descriptor)
{
    if (index >= queue_num_) {
        return false;
    }

    const auto address =
        descriptor_table_ +
        static_cast<std::uint64_t>(index) * 16U;
    const auto guest_address =
        dma_read_value(dma, address, AccessWidth::DoubleWord);
    const auto length =
        dma_read_value(dma, address + 8U, AccessWidth::Word);
    const auto flags =
        dma_read_value(dma, address + 12U, AccessWidth::HalfWord);
    const auto next =
        dma_read_value(dma, address + 14U, AccessWidth::HalfWord);

    if (!guest_address.ok() || !length.ok() ||
        !flags.ok() || !next.ok()) {
        return false;
    }

    descriptor = {
        .address = guest_address.value,
        .length = static_cast<std::uint32_t>(length.value),
        .flags = static_cast<std::uint16_t>(flags.value),
        .next = static_cast<std::uint16_t>(next.value),
    };
    return true;
}

ReadResult VirtioBlock::dma_read_value(
    platform::DmaAccess& dma,
    PhysAddr address,
    AccessWidth width)
{
    return dma.dma_read(address, width);
}

bool VirtioBlock::dma_write_value(
    platform::DmaAccess& dma,
    PhysAddr address,
    AccessWidth width,
    std::uint64_t value)
{
    return dma.dma_write(address, width, value) == BusFault::None;
}

bool VirtioBlock::queue_configured() const noexcept
{
    return queue_select_ == 0 &&
           guest_page_size_ != 0 &&
           queue_pfn_ != 0 &&
           queue_num_ != 0 &&
           queue_num_ <= maximum_queue_size &&
           is_power_of_two(queue_num_) &&
           is_power_of_two(queue_align_);
}

} // namespace rv::devices
