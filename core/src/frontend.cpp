#include "rv32/core/frontend.hpp"

#include <algorithm>

#include "rv32/core/mmu.hpp"

namespace rv32 {

namespace {

struct HalfwordFetchResult {
    FrontendStatus status{FrontendStatus::InstructionAccessFault};
    std::uint16_t value{};
    PhysAddr physical_address{};
    BusFault bus_fault{BusFault::None};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == FrontendStatus::Ready;
    }
};

[[nodiscard]] HalfwordFetchResult translate_fetch_address(
    CpuBus& bus,
    std::uint32_t virtual_address,
    const CpuSnapshot* state,
    Sv32Tlb* tlb,
    MmuPerformanceCounters* mmu_counters)
{
    PhysAddr physical_address =
        static_cast<PhysAddr>(virtual_address);
    if (state != nullptr) {
        const TranslationResult translation =
            tlb == nullptr
                ? translate_address(
                      bus,
                      *state,
                      virtual_address,
                      MemoryAccessType::InstructionFetch)
                : tlb->translate(
                      bus,
                      *state,
                      virtual_address,
                      MemoryAccessType::InstructionFetch,
                      mmu_counters);
        if (!translation.ready()) {
            return {
                .status =
                    translation.status == TranslationStatus::PageFault
                        ? FrontendStatus::InstructionPageFault
                        : FrontendStatus::InstructionAccessFault,
                .value = 0,
                .physical_address = 0,
                .bus_fault = translation.bus_fault,
                .trap_value = virtual_address,
            };
        }
        physical_address = translation.physical_address;
    }

    return {
        .status = FrontendStatus::Ready,
        .value = 0,
        .physical_address = physical_address,
        .bus_fault = BusFault::None,
        .trap_value = 0,
    };
}

[[nodiscard]] HalfwordFetchResult read_halfword(
    CpuBus& bus,
    std::uint32_t virtual_address,
    PhysAddr physical_address)
{
    const ReadResult read_result = bus.read(
        physical_address,
        AccessWidth::HalfWord,
        AccessKind::InstructionFetch);
    if (!read_result.ok()) {
        return {
            .status =
                read_result.fault == BusFault::Misaligned
                    ? FrontendStatus::InstructionAddressMisaligned
                    : FrontendStatus::InstructionAccessFault,
            .value = 0,
            .physical_address = physical_address,
            .bus_fault = read_result.fault,
            .trap_value = virtual_address,
        };
    }

    return {
        .status = FrontendStatus::Ready,
        .value = static_cast<std::uint16_t>(read_result.value),
        .physical_address = physical_address,
        .bus_fault = BusFault::None,
        .trap_value = 0,
    };
}

[[nodiscard]] HalfwordFetchResult fetch_halfword(
    CpuBus& bus,
    std::uint32_t virtual_address,
    const CpuSnapshot* state,
    Sv32Tlb* tlb,
    MmuPerformanceCounters* mmu_counters)
{
    const HalfwordFetchResult translated =
        translate_fetch_address(
            bus,
            virtual_address,
            state,
            tlb,
            mmu_counters);
    if (!translated.ready()) {
        return translated;
    }
    return read_halfword(
        bus,
        virtual_address,
        translated.physical_address);
}

[[nodiscard]] FrontendResult fetch_failure(
    std::uint32_t pc,
    std::uint32_t partial_instruction,
    const HalfwordFetchResult& failure)
{
    return {
        .status = failure.status,
        .pc = pc,
        .instruction = partial_instruction,
        .decoded = {},
        .bus_fault = failure.bus_fault,
        .trap_value = failure.trap_value,
    };
}

[[nodiscard]] constexpr std::uint16_t instruction_asid(
    std::uint32_t satp) noexcept
{
    return static_cast<std::uint16_t>(
        (satp & satp_bits::asid) >> 22U);
}

[[nodiscard]] constexpr std::size_t page_epoch_index(
    std::uint32_t virtual_address) noexcept
{
    const std::uint32_t page = virtual_address >> 12U;
    return static_cast<std::size_t>(
        page ^ (page >> 12U)) %
        (InstructionCache::entry_count / 4U);
}

[[nodiscard]] constexpr std::size_t asid_page_epoch_index(
    std::uint32_t virtual_address,
    std::uint16_t asid) noexcept
{
    const std::uint32_t page = virtual_address >> 12U;
    return static_cast<std::size_t>(
        page ^ (page >> 12U) ^
        static_cast<std::uint32_t>(asid) * 0x9E37U) %
        (InstructionCache::entry_count / 4U);
}

} // namespace

FrontendResult fetch_decode(
    CpuBus& bus,
    std::uint32_t pc,
    const CpuSnapshot* state,
    Sv32Tlb* tlb,
    MmuPerformanceCounters* mmu_counters,
    DecodeCache* decode_cache,
    DecodePerformanceCounters* decode_counters,
    InstructionCache* instruction_cache,
    InstructionCachePerformanceCounters*
        instruction_cache_counters)
{
    // RV32C uses IALIGN=16. Both compressed and 32-bit instructions may
    // begin at any two-byte boundary.
    if ((pc & 0x1U) != 0U) {
        return {
            .status = FrontendStatus::InstructionAddressMisaligned,
            .pc = pc,
            .instruction = 0,
            .decoded = {},
            .bus_fault = BusFault::Misaligned,
            .trap_value = pc,
        };
    }

    const bool may_use_instruction_cache =
        instruction_cache != nullptr &&
        (pc & 0xFFFU) != 0xFFEU;
    HalfwordFetchResult first;
    if (may_use_instruction_cache) {
        const auto cached = instruction_cache->lookup(
            pc,
            state,
            instruction_cache_counters);
        if (cached.has_value()) {
            return {
                .status = FrontendStatus::Ready,
                .pc = pc,
                .instruction = cached->instruction,
                .decoded = cached->decoded,
                .bus_fault = BusFault::None,
                .trap_value = 0,
            };
        }
        first = translate_fetch_address(
            bus,
            pc,
            state,
            tlb,
            mmu_counters);
        if (!first.ready()) {
            return fetch_failure(pc, 0, first);
        }
        first = read_halfword(bus, pc, first.physical_address);
    } else {
        first = fetch_halfword(bus, pc, state, tlb, mmu_counters);
    }
    if (!first.ready()) {
        return fetch_failure(pc, 0, first);
    }

    std::uint32_t instruction = first.value;
    const bool instruction_cache_enabled =
        may_use_instruction_cache &&
        bus.instruction_cacheable(first.physical_address) &&
        (pc & 0xFFFU) != 0xFFEU;

    DecodedInstruction decoded;
    if ((first.value & 0x3U) != 0x3U) {
        decoded =
            decode_cache == nullptr
                ? decode_compressed_instruction(first.value)
                : decode_cache->decode(
                      first.value,
                      2U,
                      decode_counters);
    } else {
        const std::uint32_t second_address = pc + 2U;
        const HalfwordFetchResult second =
            fetch_halfword(
                bus,
                second_address,
                state,
                tlb,
                mmu_counters);
        if (!second.ready()) {
            return fetch_failure(pc, instruction, second);
        }
        instruction |=
            static_cast<std::uint32_t>(second.value) << 16U;
        decoded =
            decode_cache == nullptr
                ? decode_instruction(instruction)
                : decode_cache->decode(
                      instruction,
                      4U,
                      decode_counters);
    }

    if (!decoded.valid()) {
        return {
            .status = FrontendStatus::IllegalInstruction,
            .pc = pc,
            .instruction = instruction,
            .decoded = decoded,
            .bus_fault = BusFault::None,
            .trap_value = instruction,
        };
    }

    if (instruction_cache_enabled) {
        instruction_cache->insert(
            pc,
            state,
            {
                .instruction = instruction,
                .decoded = decoded,
            });
    }

    return {
        .status = FrontendStatus::Ready,
        .pc = pc,
        .instruction = instruction,
        .decoded = decoded,
        .bus_fault = BusFault::None,
        .trap_value = 0,
    };
}

std::optional<CachedInstruction> InstructionCache::lookup(
    std::uint32_t virtual_address,
    const CpuSnapshot* state,
    InstructionCachePerformanceCounters* counters) const noexcept
{
    if (counters != nullptr) {
        ++counters->lookups;
    }
    const std::uint32_t satp =
        state == nullptr ? 0U : sanitize_satp(state->supervisor_csrs.satp);
    const PrivilegeMode privilege =
        state == nullptr ? PrivilegeMode::Machine : state->privilege;
    const std::uint16_t asid = instruction_asid(satp);
    const std::size_t page_index =
        page_epoch_index(virtual_address);
    const std::size_t asid_page_index =
        asid_page_epoch_index(virtual_address, asid);
    const std::size_t index =
        static_cast<std::size_t>(
            (virtual_address >> 1U) ^
            (virtual_address >> 13U) ^
            satp ^
            (satp >> 11U) ^
            static_cast<std::uint32_t>(privilege)) %
        entries_.size();
    const Entry& entry = entries_[index];
    if (entry.generation == generation_ &&
        entry.virtual_address == virtual_address &&
        entry.satp == satp &&
        entry.privilege == privilege &&
        entry.asid_epoch == asid_epochs_[asid] &&
        entry.page_epoch == page_epochs_[page_index] &&
        entry.asid_page_epoch ==
            asid_page_epochs_[asid_page_index]) {
        if (counters != nullptr) {
            ++counters->hits;
        }
        return entry.instruction;
    }
    if (counters != nullptr) {
        ++counters->misses;
    }
    return std::nullopt;
}

void InstructionCache::insert(
    std::uint32_t virtual_address,
    const CpuSnapshot* state,
    const CachedInstruction& instruction) noexcept
{
    const std::uint32_t satp =
        state == nullptr ? 0U : sanitize_satp(state->supervisor_csrs.satp);
    const PrivilegeMode privilege =
        state == nullptr ? PrivilegeMode::Machine : state->privilege;
    const std::uint16_t asid = instruction_asid(satp);
    const std::size_t page_index =
        page_epoch_index(virtual_address);
    const std::size_t asid_page_index =
        asid_page_epoch_index(virtual_address, asid);
    const std::size_t index =
        static_cast<std::size_t>(
            (virtual_address >> 1U) ^
            (virtual_address >> 13U) ^
            satp ^
            (satp >> 11U) ^
            static_cast<std::uint32_t>(privilege)) %
        entries_.size();
    entries_[index] = {
        .generation = generation_,
        .virtual_address = virtual_address,
        .satp = satp,
        .privilege = privilege,
        .asid_epoch = asid_epochs_[asid],
        .page_epoch = page_epochs_[page_index],
        .asid_page_epoch = asid_page_epochs_[asid_page_index],
        .instruction = instruction,
    };
}

void InstructionCache::clear(
    InstructionCachePerformanceCounters* counters) noexcept
{
    ++generation_;
    if (generation_ == 0U) {
        entries_ = {};
        asid_epochs_ = {};
        page_epochs_ = {};
        asid_page_epochs_ = {};
        generation_ = 1U;
    }
    if (counters != nullptr) {
        ++counters->invalidations;
    }
}

void InstructionCache::sfence_vma(
    std::optional<std::uint32_t> virtual_address,
    std::optional<std::uint16_t> asid,
    InstructionCachePerformanceCounters* counters) noexcept
{
    if (!virtual_address.has_value() && !asid.has_value()) {
        clear(counters);
        return;
    }

    if (virtual_address.has_value() && asid.has_value()) {
        ++asid_page_epochs_[
            asid_page_epoch_index(*virtual_address, *asid)];
    } else if (virtual_address.has_value()) {
        ++page_epochs_[page_epoch_index(*virtual_address)];
    } else {
        ++asid_epochs_[*asid];
    }
    if (counters != nullptr) {
        ++counters->invalidations;
    }
}

std::size_t InstructionCache::valid_entries() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        entries_.begin(),
        entries_.end(),
        [this](const Entry& entry) {
            if (entry.generation != generation_) {
                return false;
            }
            const std::uint16_t asid = instruction_asid(entry.satp);
            return
                entry.asid_epoch == asid_epochs_[asid] &&
                entry.page_epoch ==
                    page_epochs_[page_epoch_index(
                        entry.virtual_address)] &&
                entry.asid_page_epoch ==
                    asid_page_epochs_[asid_page_epoch_index(
                        entry.virtual_address,
                        asid)];
        }));
}

DecodedInstruction DecodeCache::decode(
    std::uint32_t instruction,
    std::uint8_t length,
    DecodePerformanceCounters* counters) noexcept
{
    if (counters != nullptr) {
        ++counters->lookups;
    }
    const std::size_t index =
        static_cast<std::size_t>(
            instruction ^
            (instruction >> 11U) ^
            (instruction >> 21U) ^
            length) %
        entries_.size();
    Entry& entry = entries_[index];
    if (entry.valid &&
        entry.instruction == instruction &&
        entry.length == length) {
        if (counters != nullptr) {
            ++counters->hits;
        }
        return entry.decoded;
    }

    if (counters != nullptr) {
        ++counters->misses;
    }
    const DecodedInstruction decoded =
        length == 2U
            ? decode_compressed_instruction(
                  static_cast<std::uint16_t>(instruction))
            : decode_instruction(instruction);
    entry = {
        .valid = true,
        .length = length,
        .instruction = instruction,
        .decoded = decoded,
    };
    return decoded;
}

void DecodeCache::clear(
    DecodePerformanceCounters* counters) noexcept
{
    entries_ = {};
    if (counters != nullptr) {
        ++counters->invalidations;
    }
}

std::size_t DecodeCache::valid_entries() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        entries_.begin(),
        entries_.end(),
        [](const Entry& entry) {
            return entry.valid;
        }));
}

} // namespace rv32
