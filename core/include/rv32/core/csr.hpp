#pragma once

#include <cstdint>

#include "rv32/core/bus.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

using CsrAddress = std::uint16_t;

enum class CsrAccessStatus : std::uint8_t {
    Ready,
    NotFound,
    PrivilegeViolation,
    ReadOnly,
};

struct CsrReadResult {
    CsrAccessStatus status{CsrAccessStatus::NotFound};
    std::uint32_t value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == CsrAccessStatus::Ready;
    }
};

[[nodiscard]] constexpr bool csr_is_read_only(
    CsrAddress address) noexcept
{
    return ((address >> 10U) & 0x3U) == 0x3U;
}

[[nodiscard]] constexpr std::uint8_t csr_minimum_privilege(
    CsrAddress address) noexcept
{
    return static_cast<std::uint8_t>((address >> 8U) & 0x3U);
}

[[nodiscard]] constexpr bool csr_privilege_allows(
    CsrAddress address,
    PrivilegeMode privilege) noexcept
{
    return static_cast<std::uint8_t>(privilege) >=
           csr_minimum_privilege(address);
}

class CsrAccess {
  public:
    virtual ~CsrAccess() = default;

    [[nodiscard]] virtual CsrReadResult read(
        CsrAddress address,
        PrivilegeMode privilege) noexcept = 0;

    [[nodiscard]] virtual CsrAccessStatus validate_write(
        CsrAddress address,
        PrivilegeMode privilege) noexcept = 0;

    virtual void write_validated(
        CsrAddress address,
        std::uint32_t value) noexcept = 0;

    // Some pending CSRs are the OR of a software-writable bit and a hardware
    // line. CSRRS/CSRRC must modify only the software part.
    [[nodiscard]] virtual std::uint32_t read_for_write(
        CsrAddress address,
        std::uint32_t read_value) noexcept
    {
        static_cast<void>(address);
        return read_value;
    }
};

namespace csr_address {

inline constexpr CsrAddress sstatus = 0x100U;
inline constexpr CsrAddress sie = 0x104U;
inline constexpr CsrAddress stvec = 0x105U;
inline constexpr CsrAddress scounteren = 0x106U;
inline constexpr CsrAddress sscratch = 0x140U;
inline constexpr CsrAddress sepc = 0x141U;
inline constexpr CsrAddress scause = 0x142U;
inline constexpr CsrAddress stval = 0x143U;
inline constexpr CsrAddress sip = 0x144U;
inline constexpr CsrAddress satp = 0x180U;

inline constexpr CsrAddress mstatus = 0x300U;
inline constexpr CsrAddress misa = 0x301U;
inline constexpr CsrAddress medeleg = 0x302U;
inline constexpr CsrAddress mideleg = 0x303U;
inline constexpr CsrAddress mie = 0x304U;
inline constexpr CsrAddress mtvec = 0x305U;
inline constexpr CsrAddress mcounteren = 0x306U;
inline constexpr CsrAddress mstatush = 0x310U;
inline constexpr CsrAddress medelegh = 0x312U;
inline constexpr CsrAddress mscratch = 0x340U;
inline constexpr CsrAddress mepc = 0x341U;
inline constexpr CsrAddress mcause = 0x342U;
inline constexpr CsrAddress mtval = 0x343U;
inline constexpr CsrAddress mip = 0x344U;

inline constexpr CsrAddress mcycle = 0xB00U;
inline constexpr CsrAddress minstret = 0xB02U;
inline constexpr CsrAddress mcycleh = 0xB80U;
inline constexpr CsrAddress minstreth = 0xB82U;

inline constexpr CsrAddress cycle = 0xC00U;
inline constexpr CsrAddress time = 0xC01U;
inline constexpr CsrAddress instret = 0xC02U;
inline constexpr CsrAddress cycleh = 0xC80U;
inline constexpr CsrAddress timeh = 0xC81U;
inline constexpr CsrAddress instreth = 0xC82U;

inline constexpr CsrAddress mvendorid = 0xF11U;
inline constexpr CsrAddress marchid = 0xF12U;
inline constexpr CsrAddress mimpid = 0xF13U;
inline constexpr CsrAddress mhartid = 0xF14U;

} // namespace csr_address

namespace mstatus_bits {

inline constexpr std::uint32_t sie = 1U << 1U;
inline constexpr std::uint32_t mie = 1U << 3U;
inline constexpr std::uint32_t spie = 1U << 5U;
inline constexpr std::uint32_t mpie = 1U << 7U;
inline constexpr std::uint32_t spp = 1U << 8U;
inline constexpr unsigned int mpp_shift = 11U;
inline constexpr std::uint32_t mpp = 0x3U << mpp_shift;
inline constexpr std::uint32_t mprv = 1U << 17U;
inline constexpr std::uint32_t sum = 1U << 18U;
inline constexpr std::uint32_t mxr = 1U << 19U;
inline constexpr std::uint32_t tvm = 1U << 20U;
inline constexpr std::uint32_t tw = 1U << 21U;
inline constexpr std::uint32_t tsr = 1U << 22U;
inline constexpr std::uint32_t supervisor_view =
    sie | spie | spp | sum | mxr;
inline constexpr std::uint32_t implemented =
    supervisor_view | mie | mpie | mpp | mprv | tvm | tw | tsr;

} // namespace mstatus_bits

inline constexpr std::uint32_t supported_exception_delegation =
    0x000003FFU |
    (1U << 12U) |
    (1U << 13U) |
    (1U << 15U);
inline constexpr std::uint32_t supported_interrupt_delegation =
    (1U << 1U) | (1U << 5U) | (1U << 9U);
inline constexpr std::uint32_t supported_counter_enable = 0x7U;

// RV32IMAC with implemented Supervisor and User privilege modes.
inline constexpr std::uint32_t machine_isa_value = 0x40141105U;

[[nodiscard]] std::uint32_t sanitize_mstatus(
    std::uint32_t value) noexcept;

[[nodiscard]] std::uint32_t sanitize_mtvec(
    std::uint32_t value) noexcept;

class CsrFile final : public CsrAccess {
  public:
    CsrFile(CpuSnapshot& state, CpuBus& bus) noexcept;

    [[nodiscard]] CsrReadResult read(
        CsrAddress address,
        PrivilegeMode privilege) noexcept override;

    [[nodiscard]] CsrAccessStatus validate_write(
        CsrAddress address,
        PrivilegeMode privilege) noexcept override;

    void write_validated(
        CsrAddress address,
        std::uint32_t value) noexcept override;

    [[nodiscard]] std::uint32_t read_for_write(
        CsrAddress address,
        std::uint32_t read_value) noexcept override;

  private:
    CpuSnapshot* state_;
    CpuBus* bus_;
};

} // namespace rv32
