#include "rv/devices/virtio_net.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

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

} // namespace

VirtioNet::VirtioNet(
    PhysAddr base,
    std::uint64_t size,
    MacAddress mac)
    : range_{.base = base, .size = size},
      mac_(mac)
{
    reset();
}

std::string_view VirtioNet::name() const noexcept
{
    return "VirtIO MMIO Network";
}

platform::AddressRange VirtioNet::range() const noexcept
{
    return range_;
}

ReadResult VirtioNet::read(
    std::uint64_t offset,
    AccessWidth width)
{
    if (offset >= config_offset) {
        std::array<std::uint8_t, 8> config{};
        std::copy(mac_.begin(), mac_.end(), config.begin());
        config[6] = static_cast<std::uint8_t>(link_up & 0xFFU);
        config[7] = static_cast<std::uint8_t>(link_up >> 8U);
        return platform::read_little_endian(
            config,
            offset - config_offset,
            width);
    }

    if (width != AccessWidth::Word || (offset & 0x3U) != 0) {
        return {.fault = BusFault::Unsupported};
    }

    std::uint32_t value = 0;
    const auto* const queue = selected_queue_entry();
    switch (offset) {
    case magic_value_offset:
        value = magic_value;
        break;
    case version_offset:
        value = legacy_version;
        break;
    case device_id_offset:
        value = network_device_id;
        break;
    case vendor_id_offset:
        value = vendor_id;
        break;
    case device_features_offset:
        if (device_features_select_ == 0) {
            value = feature_mac | feature_status;
        }
        break;
    case queue_num_max_offset:
        value = queue == nullptr ? 0U : maximum_queue_size;
        break;
    case queue_num_offset:
        value = queue == nullptr ? 0U : queue->size;
        break;
    case queue_align_offset:
        value = queue == nullptr ? 0U : queue->alignment;
        break;
    case queue_pfn_offset:
        value = queue == nullptr ? 0U : queue->page_frame_number;
        break;
    case interrupt_status_offset:
        value = interrupt_status_;
        break;
    case status_offset:
        value = device_status_;
        break;
    default:
        break;
    }

    return {
        .fault = BusFault::None,
        .value = value,
    };
}

BusFault VirtioNet::write(
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
            driver_features_ = word & (feature_mac | feature_status);
        }
        break;
    case driver_features_select_offset:
        driver_features_select_ = word;
        break;
    case guest_page_size_offset:
        guest_page_size_ = word;
        for (auto& queue : queues_) {
            update_queue_addresses(queue);
        }
        break;
    case queue_select_offset:
        queue_select_ = word;
        break;
    case queue_num_offset:
        if (auto* const queue = selected_queue_entry()) {
            queue->size = static_cast<std::uint16_t>(word);
            update_queue_addresses(*queue);
        }
        break;
    case queue_align_offset:
        if (auto* const queue = selected_queue_entry()) {
            queue->alignment = word;
            update_queue_addresses(*queue);
        }
        break;
    case queue_pfn_offset:
        if (auto* const queue = selected_queue_entry()) {
            queue->page_frame_number = word;
            update_queue_addresses(*queue);
        }
        break;
    case queue_notify_offset:
        ++statistics_.queue_notify_writes;
        if (word < queues_.size() &&
            queue_configured(queues_[word])) {
            queues_[word].notification_pending = true;
            ++statistics_.accepted_notifications;
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

void VirtioNet::tick(
    platform::DmaAccess& dma,
    std::uint64_t cycles)
{
    collect_backend_frames(cycles);

    if (queues_[transmit_queue].notification_pending) {
        queues_[transmit_queue].notification_pending = false;
        process_transmit_queue(dma);
    }

    if (queues_[receive_queue].notification_pending) {
        queues_[receive_queue].notification_pending = false;
    }
    if (!pending_receive_frames_.empty()) {
        process_receive_queue(dma);
    }
}

void VirtioNet::set_backend(NetworkBackend* backend) noexcept
{
    backend_ = backend;
}

NetworkBackend* VirtioNet::backend() const noexcept
{
    return backend_;
}

bool VirtioNet::inject_received_frame(
    std::span<const std::uint8_t> frame)
{
    if (frame.empty() || frame.size() > maximum_frame_size ||
        pending_receive_frames_.size() >= maximum_pending_frames) {
        ++statistics_.dropped_receive_frames;
        return false;
    }
    pending_receive_frames_.emplace_back(frame.begin(), frame.end());
    statistics_.pending_receive_frames =
        pending_receive_frames_.size();
    statistics_.peak_pending_receive_frames = std::max(
        statistics_.peak_pending_receive_frames,
        statistics_.pending_receive_frames);
    return true;
}

bool VirtioNet::interrupt_pending() const noexcept
{
    return (interrupt_status_ & 0x1U) != 0;
}

VirtioNet::MacAddress VirtioNet::mac_address() const noexcept
{
    return mac_;
}

std::uint8_t VirtioNet::device_status() const noexcept
{
    return device_status_;
}

std::uint32_t VirtioNet::selected_queue() const noexcept
{
    return queue_select_;
}

VirtioNetQueueState VirtioNet::queue_state(
    std::uint32_t queue) const noexcept
{
    if (queue >= queues_.size()) {
        return {};
    }
    const auto& entry = queues_[queue];
    return {
        .queue_size = entry.size,
        .alignment = entry.alignment,
        .page_frame_number = entry.page_frame_number,
        .last_available_index = entry.last_available_index,
        .used_index = entry.used_index,
        .configured = queue_configured(entry),
    };
}

const VirtioNetStatistics& VirtioNet::statistics() const noexcept
{
    return statistics_;
}

void VirtioNet::reset() noexcept
{
    device_features_select_ = 0;
    driver_features_select_ = 0;
    driver_features_ = 0;
    guest_page_size_ = 0;
    queue_select_ = 0;
    interrupt_status_ = 0;
    device_status_ = 0;
    queues_ = {};
    pending_receive_frames_.clear();
    statistics_.pending_receive_frames = 0U;
}

void VirtioNet::update_queue_addresses(Queue& queue) noexcept
{
    if (!queue_configured(queue)) {
        queue.descriptor_table = 0;
        queue.available_ring = 0;
        queue.used_ring = 0;
        return;
    }

    queue.descriptor_table =
        static_cast<PhysAddr>(queue.page_frame_number) *
        static_cast<std::uint64_t>(guest_page_size_);
    queue.available_ring =
        queue.descriptor_table +
        static_cast<std::uint64_t>(queue.size) * 16U;
    const auto available_size =
        4U + static_cast<std::uint64_t>(queue.size) * 2U;
    queue.used_ring = align_up(
        queue.available_ring + available_size,
        queue.alignment);
}

void VirtioNet::collect_backend_frames(std::uint64_t cycles)
{
    if (backend_ == nullptr) {
        return;
    }

    backend_->tick(cycles);
    while (pending_receive_frames_.size() < maximum_pending_frames) {
        auto frame = backend_->receive_frame();
        if (!frame.has_value()) {
            break;
        }
        if (frame->empty() || frame->size() > maximum_frame_size) {
            ++statistics_.dropped_receive_frames;
            continue;
        }
        pending_receive_frames_.push_back(std::move(*frame));
        statistics_.pending_receive_frames =
            pending_receive_frames_.size();
        statistics_.peak_pending_receive_frames = std::max(
            statistics_.peak_pending_receive_frames,
            statistics_.pending_receive_frames);
    }
}

void VirtioNet::process_transmit_queue(platform::DmaAccess& dma)
{
    auto& queue = queues_[transmit_queue];
    if (!queue_configured(queue)) {
        return;
    }

    std::uint16_t processed = 0;
    while (processed < queue.size) {
        const auto available_index = dma_read_value(
            dma,
            queue.available_ring + 2U,
            AccessWidth::HalfWord);
        if (!available_index.ok()) {
            ++statistics_.dma_failures;
            break;
        }
        if (queue.last_available_index ==
            static_cast<std::uint16_t>(available_index.value)) {
            break;
        }

        std::uint16_t head = 0;
        if (!read_available_head(dma, queue, head)) {
            break;
        }

        std::vector<Descriptor> chain;
        ++statistics_.descriptor_chains;
        const bool chain_valid =
            load_chain(dma, queue, head, chain);
        const bool transmitted =
            chain_valid && transmit_chain(dma, chain);
        if (!transmitted) {
            ++statistics_.dropped_transmit_frames;
        }
        if (!complete_chain(dma, queue, head, 0)) {
            break;
        }

        ++queue.last_available_index;
        ++processed;
        interrupt_status_ |= 0x1U;
        ++statistics_.interrupts_raised;
    }
}

void VirtioNet::process_receive_queue(platform::DmaAccess& dma)
{
    auto& queue = queues_[receive_queue];
    if (!queue_configured(queue)) {
        return;
    }

    std::uint16_t processed = 0;
    while (!pending_receive_frames_.empty() &&
           processed < queue.size) {
        const auto available_index = dma_read_value(
            dma,
            queue.available_ring + 2U,
            AccessWidth::HalfWord);
        if (!available_index.ok()) {
            ++statistics_.dma_failures;
            break;
        }
        if (queue.last_available_index ==
            static_cast<std::uint16_t>(available_index.value)) {
            ++statistics_.receive_queue_starvations;
            break;
        }

        std::uint16_t head = 0;
        if (!read_available_head(dma, queue, head)) {
            break;
        }

        std::vector<Descriptor> chain;
        std::uint32_t bytes_written = 0;
        ++statistics_.descriptor_chains;
        const bool chain_valid =
            load_chain(dma, queue, head, chain);
        const bool received =
            chain_valid &&
            receive_chain(
                dma,
                chain,
                pending_receive_frames_.front(),
                bytes_written);
        if (!received) {
            ++statistics_.dropped_receive_frames;
            bytes_written = 0;
        }
        pending_receive_frames_.pop_front();
        statistics_.pending_receive_frames =
            pending_receive_frames_.size();

        if (!complete_chain(
                dma,
                queue,
                head,
                bytes_written)) {
            break;
        }
        ++queue.last_available_index;
        ++processed;
        interrupt_status_ |= 0x1U;
        ++statistics_.interrupts_raised;
    }
}

bool VirtioNet::load_chain(
    platform::DmaAccess& dma,
    const Queue& queue,
    std::uint16_t head,
    std::vector<Descriptor>& chain)
{
    chain.clear();
    if (head >= queue.size) {
        return false;
    }

    std::vector<bool> visited(queue.size, false);
    auto current = head;
    for (std::uint16_t count = 0; count < queue.size; ++count) {
        if (current >= queue.size ||
            visited[static_cast<std::size_t>(current)]) {
            return false;
        }
        visited[static_cast<std::size_t>(current)] = true;

        const auto address =
            queue.descriptor_table +
            static_cast<std::uint64_t>(current) * 16U;
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
            ++statistics_.dma_failures;
            return false;
        }

        chain.push_back({
            .address = guest_address.value,
            .length = static_cast<std::uint32_t>(length.value),
            .flags = static_cast<std::uint16_t>(flags.value),
            .next = static_cast<std::uint16_t>(next.value),
        });
        if ((chain.back().flags & descriptor_has_next) == 0) {
            return true;
        }
        current = chain.back().next;
    }
    return false;
}

bool VirtioNet::read_available_head(
    platform::DmaAccess& dma,
    const Queue& queue,
    std::uint16_t& head)
{
    const auto ring_entry =
        queue.available_ring + 4U +
        static_cast<std::uint64_t>(
            queue.last_available_index % queue.size) *
            sizeof(std::uint16_t);
    const auto result =
        dma_read_value(dma, ring_entry, AccessWidth::HalfWord);
    if (!result.ok()) {
        ++statistics_.dma_failures;
        return false;
    }
    head = static_cast<std::uint16_t>(result.value);
    return true;
}

bool VirtioNet::complete_chain(
    platform::DmaAccess& dma,
    Queue& queue,
    std::uint16_t head,
    std::uint32_t bytes_written)
{
    const auto used_element =
        queue.used_ring + 4U +
        static_cast<std::uint64_t>(queue.used_index % queue.size) * 8U;
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
        ++statistics_.dma_failures;
        return false;
    }

    const auto next_used =
        static_cast<std::uint16_t>(queue.used_index + 1U);
    if (!dma_write_value(
            dma,
            queue.used_ring + 2U,
            AccessWidth::HalfWord,
            next_used)) {
        ++statistics_.dma_failures;
        return false;
    }
    queue.used_index = next_used;
    return true;
}

bool VirtioNet::transmit_chain(
    platform::DmaAccess& dma,
    const std::vector<Descriptor>& chain)
{
    std::size_t total_size = 0;
    for (const auto& descriptor : chain) {
        if ((descriptor.flags & descriptor_is_write) != 0 ||
            descriptor.length >
                maximum_frame_size + net_header_size - total_size) {
            return false;
        }
        total_size += descriptor.length;
    }
    if (total_size <= net_header_size) {
        return false;
    }

    std::vector<std::uint8_t> packet;
    packet.reserve(total_size);
    for (const auto& descriptor : chain) {
        for (std::uint32_t index = 0;
             index < descriptor.length;
             ++index) {
            const auto byte = dma_read_value(
                dma,
                descriptor.address + index,
                AccessWidth::Byte);
            if (!byte.ok()) {
                ++statistics_.dma_failures;
                return false;
            }
            packet.push_back(static_cast<std::uint8_t>(byte.value));
        }
    }

    const std::span<const std::uint8_t> frame{
        packet.data() + net_header_size,
        packet.size() - net_header_size};
    if (backend_ == nullptr) {
        return false;
    }
    backend_->send_frame(frame);
    ++statistics_.transmitted_frames;
    statistics_.transmitted_bytes += frame.size();
    return true;
}

bool VirtioNet::receive_chain(
    platform::DmaAccess& dma,
    const std::vector<Descriptor>& chain,
    std::span<const std::uint8_t> frame,
    std::uint32_t& bytes_written)
{
    bytes_written = 0;
    std::size_t capacity = 0;
    for (const auto& descriptor : chain) {
        if ((descriptor.flags & descriptor_is_write) == 0 ||
            descriptor.length >
                std::numeric_limits<std::size_t>::max() - capacity) {
            return false;
        }
        capacity += descriptor.length;
    }

    const auto required = net_header_size + frame.size();
    if (capacity < required ||
        required >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }

    std::size_t source_index = 0;
    for (const auto& descriptor : chain) {
        for (std::uint32_t index = 0;
             index < descriptor.length &&
             source_index < required;
             ++index, ++source_index) {
            const std::uint8_t byte =
                source_index < net_header_size
                    ? 0U
                    : frame[source_index - net_header_size];
            if (!dma_write_value(
                    dma,
                    descriptor.address + index,
                    AccessWidth::Byte,
                    byte)) {
                ++statistics_.dma_failures;
                return false;
            }
        }
    }

    bytes_written = static_cast<std::uint32_t>(required);
    ++statistics_.received_frames;
    statistics_.received_bytes += frame.size();
    return true;
}

ReadResult VirtioNet::dma_read_value(
    platform::DmaAccess& dma,
    PhysAddr address,
    AccessWidth width)
{
    return dma.dma_read(address, width);
}

bool VirtioNet::dma_write_value(
    platform::DmaAccess& dma,
    PhysAddr address,
    AccessWidth width,
    std::uint64_t value)
{
    return dma.dma_write(address, width, value) == BusFault::None;
}

bool VirtioNet::queue_configured(
    const Queue& queue) const noexcept
{
    return guest_page_size_ != 0 &&
           is_power_of_two(guest_page_size_) &&
           queue.page_frame_number != 0 &&
           queue.size != 0 &&
           queue.size <= maximum_queue_size &&
           is_power_of_two(queue.size) &&
           is_power_of_two(queue.alignment);
}

VirtioNet::Queue* VirtioNet::selected_queue_entry() noexcept
{
    if (queue_select_ >= queues_.size()) {
        return nullptr;
    }
    return &queues_[queue_select_];
}

const VirtioNet::Queue* VirtioNet::selected_queue_entry() const noexcept
{
    if (queue_select_ >= queues_.size()) {
        return nullptr;
    }
    return &queues_[queue_select_];
}

} // namespace rv::devices
