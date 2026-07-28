#pragma once

#include <cstdint>

namespace rv32 {

using PhysAddr = std::uint64_t;

enum class AccessWidth : std::uint8_t {
    Byte = 1,
    HalfWord = 2,
    Word = 4,
    DoubleWord = 8,
};

[[nodiscard]] constexpr std::uint8_t width_bytes(AccessWidth width) noexcept
{
    return static_cast<std::uint8_t>(width);
}

enum class AccessKind : std::uint8_t {
    InstructionFetch,
    Load,
    Store,
    PageTableWalk,
    Atomic,
    Dma,
};

enum class BusFault : std::uint8_t {
    None,
    Unmapped,
    Misaligned,
    OutOfRange,
    ReadOnly,
    Unsupported,
    DeviceError,
};

struct ReadResult {
    BusFault fault{BusFault::None};
    std::uint64_t value{};

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return fault == BusFault::None;
    }
};

struct StoreConditionalResult {
    BusFault fault{BusFault::None};
    bool succeeded{};

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return fault == BusFault::None;
    }
};

enum class AmoOperation : std::uint8_t {
    Swap,
    Add,
    Xor,
    And,
    Or,
    Min,
    Max,
    MinUnsigned,
    MaxUnsigned,
};

struct AtomicResult {
    BusFault fault{BusFault::None};
    std::uint32_t original_value{};

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return fault == BusFault::None;
    }
};

class CpuBus {
  public:
    virtual ~CpuBus() = default;

    [[nodiscard]] virtual ReadResult read(
        PhysAddr address,
        AccessWidth width,
        AccessKind kind) = 0;

    [[nodiscard]] virtual BusFault write(
        PhysAddr address,
        AccessWidth width,
        std::uint64_t value,
        AccessKind kind) = 0;

    [[nodiscard]] virtual ReadResult load_reserved_word(
        std::uint32_t hart_id,
        PhysAddr address) = 0;

    [[nodiscard]] virtual StoreConditionalResult store_conditional_word(
        std::uint32_t hart_id,
        PhysAddr address,
        std::uint32_t value) = 0;

    [[nodiscard]] virtual AtomicResult atomic_word(
        std::uint32_t hart_id,
        PhysAddr address,
        AmoOperation operation,
        std::uint32_t operand) = 0;

    [[nodiscard]] virtual std::uint64_t read_time() const noexcept = 0;

    // Fast frontends may retain fetched instruction bytes only for memory
    // regions that explicitly opt in. MMIO remains non-cacheable by default.
    [[nodiscard]] virtual bool instruction_cacheable(
        PhysAddr address) const noexcept
    {
        static_cast<void>(address);
        return false;
    }
};

} // namespace rv32
