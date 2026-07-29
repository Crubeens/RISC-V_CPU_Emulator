#include "rv32/platform/system_bus.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace rv32::platform {

void SystemBus::add_device(std::unique_ptr<Device> device)
{
    if (!device) {
        throw std::invalid_argument("cannot add a null bus device");
    }

    const auto new_range = device->range();
    if (new_range.size == 0 ||
        new_range.size >
            std::numeric_limits<PhysAddr>::max() - new_range.base) {
        throw std::invalid_argument("bus device has an invalid address range");
    }

    const auto overlap = std::find_if(
        devices_.begin(),
        devices_.end(),
        [new_range](const auto& existing) {
            return new_range.overlaps(existing->range());
        });

    if (overlap != devices_.end()) {
        throw std::invalid_argument(
            "bus device address range overlaps an existing device");
    }

    Device* const added_device = device.get();
    devices_.push_back(std::move(device));
    std::sort(
        devices_.begin(),
        devices_.end(),
        [](const auto& left, const auto& right) {
            return left->range().base < right->range().base;
        });
    if (added_device->needs_tick()) {
        tick_devices_.push_back(added_device);
    }
    last_device_ = nullptr;
}

void SystemBus::set_time_source(const TimeSource* time_source) noexcept
{
    time_source_ = time_source;
}

std::vector<DeviceInfo> SystemBus::device_map() const
{
    std::vector<DeviceInfo> result;
    result.reserve(devices_.size());

    for (const auto& device : devices_) {
        result.push_back({
            .name = std::string(device->name()),
            .range = device->range(),
        });
    }

    return result;
}

void SystemBus::tick_devices(std::uint64_t cycles)
{
    ++performance_counters_.device_ticks;
    for (auto* device : tick_devices_) {
        device->tick(*this, cycles);
    }
}

const BusPerformanceCounters&
SystemBus::performance_counters() const noexcept
{
    return performance_counters_;
}

void SystemBus::reset_performance_counters() noexcept
{
    performance_counters_ = {};
}

void SystemBus::clear_reservations() noexcept
{
    reservations_.clear();
}

ReadResult SystemBus::read(
    PhysAddr address,
    AccessWidth width,
    AccessKind kind)
{
    const auto kind_index = static_cast<std::size_t>(kind);
    ++performance_counters_.reads[kind_index];

    auto* device = find_device(address, width);
    if (device == nullptr) {
        ++performance_counters_.faults;
        return {.fault = BusFault::Unmapped};
    }

    const ReadResult result =
        device->read(address - device->range().base, width);
    if (!result.ok()) {
        ++performance_counters_.faults;
    }
    return result;
}

BusFault SystemBus::write(
    PhysAddr address,
    AccessWidth width,
    std::uint64_t value,
    AccessKind kind)
{
    const auto kind_index = static_cast<std::size_t>(kind);
    ++performance_counters_.writes[kind_index];

    auto* device = find_device(address, width);
    if (device == nullptr) {
        ++performance_counters_.faults;
        return BusFault::Unmapped;
    }

    const auto fault =
        device->write(address - device->range().base, width, value);
    if (fault == BusFault::None) {
        ++write_epoch_;
    } else {
        ++performance_counters_.faults;
    }
    return fault;
}

ReadResult SystemBus::load_reserved_word(
    std::uint32_t hart_id,
    PhysAddr address)
{
    if ((address & 0x3ULL) != 0) {
        return {.fault = BusFault::Misaligned};
    }

    const auto result =
        read(address, AccessWidth::Word, AccessKind::Atomic);
    if (result.ok()) {
        reservations_[hart_id] = {
            .address = address,
            .width = AccessWidth::Word,
            .write_epoch = write_epoch_,
        };
    }
    return result;
}

StoreConditionalResult SystemBus::store_conditional_word(
    std::uint32_t hart_id,
    PhysAddr address,
    std::uint32_t value)
{
    if ((address & 0x3ULL) != 0) {
        return {
            .fault = BusFault::Misaligned,
            .succeeded = false,
        };
    }

    const auto reservation = reservations_.find(hart_id);
    const bool matches =
        reservation != reservations_.end() &&
        reservation->second.address == address &&
        reservation->second.width == AccessWidth::Word &&
        reservation->second.write_epoch == write_epoch_;

    if (reservation != reservations_.end()) {
        reservations_.erase(reservation);
    }

    // Even a failed SC must pass the address checks of a store. Otherwise an
    // SC without a matching reservation could incorrectly retire on an
    // unmapped address.
    if (find_device(address, AccessWidth::Word) == nullptr) {
        return {
            .fault = BusFault::Unmapped,
            .succeeded = false,
        };
    }

    if (!matches) {
        return {
            .fault = BusFault::None,
            .succeeded = false,
        };
    }

    const auto fault =
        write(address, AccessWidth::Word, value, AccessKind::Atomic);
    return {
        .fault = fault,
        .succeeded = fault == BusFault::None,
    };
}

AtomicResult SystemBus::atomic_word(
    std::uint32_t hart_id,
    PhysAddr address,
    AmoOperation operation,
    std::uint32_t operand)
{
    static_cast<void>(hart_id);

    if ((address & 0x3ULL) != 0) {
        return {.fault = BusFault::Misaligned};
    }

    const auto read_result =
        read(address, AccessWidth::Word, AccessKind::Atomic);
    if (!read_result.ok()) {
        return {.fault = read_result.fault};
    }

    const auto original = static_cast<std::uint32_t>(read_result.value);
    std::uint32_t replacement{};

    const auto signed_original = std::bit_cast<std::int32_t>(original);
    const auto signed_operand = std::bit_cast<std::int32_t>(operand);

    switch (operation) {
    case AmoOperation::Swap:
        replacement = operand;
        break;
    case AmoOperation::Add:
        replacement = original + operand;
        break;
    case AmoOperation::Xor:
        replacement = original ^ operand;
        break;
    case AmoOperation::And:
        replacement = original & operand;
        break;
    case AmoOperation::Or:
        replacement = original | operand;
        break;
    case AmoOperation::Min:
        replacement =
            std::bit_cast<std::uint32_t>(
                std::min(signed_original, signed_operand));
        break;
    case AmoOperation::Max:
        replacement =
            std::bit_cast<std::uint32_t>(
                std::max(signed_original, signed_operand));
        break;
    case AmoOperation::MinUnsigned:
        replacement = std::min(original, operand);
        break;
    case AmoOperation::MaxUnsigned:
        replacement = std::max(original, operand);
        break;
    }

    const auto fault =
        write(address, AccessWidth::Word, replacement, AccessKind::Atomic);
    return {
        .fault = fault,
        .original_value = original,
    };
}

ReadResult SystemBus::load_reserved_doubleword(
    std::uint32_t hart_id,
    PhysAddr address)
{
    if ((address & 0x7ULL) != 0U) {
        return {.fault = BusFault::Misaligned};
    }

    const auto result =
        read(address, AccessWidth::DoubleWord, AccessKind::Atomic);
    if (result.ok()) {
        reservations_[hart_id] = {
            .address = address,
            .width = AccessWidth::DoubleWord,
            .write_epoch = write_epoch_,
        };
    }
    return result;
}

StoreConditionalResult SystemBus::store_conditional_doubleword(
    std::uint32_t hart_id,
    PhysAddr address,
    std::uint64_t value)
{
    if ((address & 0x7ULL) != 0U) {
        return {
            .fault = BusFault::Misaligned,
            .succeeded = false,
        };
    }

    const auto reservation = reservations_.find(hart_id);
    const bool matches =
        reservation != reservations_.end() &&
        reservation->second.address == address &&
        reservation->second.width == AccessWidth::DoubleWord &&
        reservation->second.write_epoch == write_epoch_;
    if (reservation != reservations_.end()) {
        reservations_.erase(reservation);
    }

    if (find_device(address, AccessWidth::DoubleWord) == nullptr) {
        return {
            .fault = BusFault::Unmapped,
            .succeeded = false,
        };
    }
    if (!matches) {
        return {
            .fault = BusFault::None,
            .succeeded = false,
        };
    }

    const auto fault = write(
        address,
        AccessWidth::DoubleWord,
        value,
        AccessKind::Atomic);
    return {
        .fault = fault,
        .succeeded = fault == BusFault::None,
    };
}

AtomicResult64 SystemBus::atomic_doubleword(
    std::uint32_t hart_id,
    PhysAddr address,
    AmoOperation operation,
    std::uint64_t operand)
{
    static_cast<void>(hart_id);
    if ((address & 0x7ULL) != 0U) {
        return {.fault = BusFault::Misaligned};
    }

    const auto read_result =
        read(address, AccessWidth::DoubleWord, AccessKind::Atomic);
    if (!read_result.ok()) {
        return {.fault = read_result.fault};
    }

    const std::uint64_t original = read_result.value;
    std::uint64_t replacement{};
    const auto signed_original = std::bit_cast<std::int64_t>(original);
    const auto signed_operand = std::bit_cast<std::int64_t>(operand);
    switch (operation) {
    case AmoOperation::Swap:
        replacement = operand;
        break;
    case AmoOperation::Add:
        replacement = original + operand;
        break;
    case AmoOperation::Xor:
        replacement = original ^ operand;
        break;
    case AmoOperation::And:
        replacement = original & operand;
        break;
    case AmoOperation::Or:
        replacement = original | operand;
        break;
    case AmoOperation::Min:
        replacement = std::bit_cast<std::uint64_t>(
            std::min(signed_original, signed_operand));
        break;
    case AmoOperation::Max:
        replacement = std::bit_cast<std::uint64_t>(
            std::max(signed_original, signed_operand));
        break;
    case AmoOperation::MinUnsigned:
        replacement = std::min(original, operand);
        break;
    case AmoOperation::MaxUnsigned:
        replacement = std::max(original, operand);
        break;
    }

    const auto fault = write(
        address,
        AccessWidth::DoubleWord,
        replacement,
        AccessKind::Atomic);
    return {
        .fault = fault,
        .original_value = original,
    };
}

std::uint64_t SystemBus::read_time() const noexcept
{
    return time_source_ == nullptr ? 0 : time_source_->time_value();
}

bool SystemBus::instruction_cacheable(
    PhysAddr address) const noexcept
{
    if (last_device_ != nullptr &&
        last_device_->range().contains(
            address,
            AccessWidth::Byte)) {
        return last_device_->instruction_cacheable();
    }
    const auto upper = std::upper_bound(
        devices_.begin(),
        devices_.end(),
        address,
        [](PhysAddr value, const auto& device) {
            return value < device->range().base;
        });
    if (upper == devices_.begin()) {
        return false;
    }
    auto candidate = upper;
    --candidate;
    return
        (*candidate)->range().contains(address, AccessWidth::Byte) &&
        (*candidate)->instruction_cacheable();
}

ReadResult SystemBus::dma_read(
    PhysAddr address,
    AccessWidth width)
{
    return read(address, width, AccessKind::Dma);
}

BusFault SystemBus::dma_write(
    PhysAddr address,
    AccessWidth width,
    std::uint64_t value)
{
    return write(address, width, value, AccessKind::Dma);
}

Device* SystemBus::find_device(
    PhysAddr address,
    AccessWidth width) noexcept
{
    ++performance_counters_.device_lookups;
    if (last_device_ != nullptr &&
        last_device_->range().contains(address, width)) {
        ++performance_counters_.device_cache_hits;
        return last_device_;
    }

    const auto upper = std::upper_bound(
        devices_.begin(),
        devices_.end(),
        address,
        [](PhysAddr value, const auto& device) {
            return value < device->range().base;
        });
    if (upper == devices_.begin()) {
        return nullptr;
    }
    auto candidate = upper;
    --candidate;
    if (!(*candidate)->range().contains(address, width)) {
        return nullptr;
    }
    last_device_ = candidate->get();
    return last_device_;
}

} // namespace rv32::platform
