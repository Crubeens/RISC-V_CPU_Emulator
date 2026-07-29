#pragma once

#include <cstdint>

#include "rv/common/bus.hpp"
#include "rv64/core/types.hpp"

namespace rv64 {

using CsrAddress = std::uint16_t;

enum class CsrAccessStatus : std::uint8_t {
    Ready,
    NotFound,
    PrivilegeViolation,
    ReadOnly,
};

struct CsrReadResult {
    CsrAccessStatus status{CsrAccessStatus::NotFound};
    Xlen value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == CsrAccessStatus::Ready;
    }
};

namespace csr_address {

inline constexpr CsrAddress fflags = 0x001U;
inline constexpr CsrAddress frm = 0x002U;
inline constexpr CsrAddress fcsr = 0x003U;

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
inline constexpr CsrAddress mscratch = 0x340U;
inline constexpr CsrAddress mepc = 0x341U;
inline constexpr CsrAddress mcause = 0x342U;
inline constexpr CsrAddress mtval = 0x343U;
inline constexpr CsrAddress mip = 0x344U;

inline constexpr CsrAddress mcycle = 0xB00U;
inline constexpr CsrAddress minstret = 0xB02U;
inline constexpr CsrAddress cycle = 0xC00U;
inline constexpr CsrAddress time = 0xC01U;
inline constexpr CsrAddress instret = 0xC02U;

inline constexpr CsrAddress mvendorid = 0xF11U;
inline constexpr CsrAddress marchid = 0xF12U;
inline constexpr CsrAddress mimpid = 0xF13U;
inline constexpr CsrAddress mhartid = 0xF14U;

} // namespace csr_address

namespace mstatus_bits {

inline constexpr Xlen sie = Xlen{1} << 1U;
inline constexpr Xlen mie = Xlen{1} << 3U;
inline constexpr Xlen spie = Xlen{1} << 5U;
inline constexpr Xlen mpie = Xlen{1} << 7U;
inline constexpr Xlen spp = Xlen{1} << 8U;
inline constexpr unsigned int mpp_shift = 11U;
inline constexpr Xlen mpp = Xlen{3} << mpp_shift;
inline constexpr unsigned int fs_shift = 13U;
inline constexpr Xlen fs = Xlen{3} << fs_shift;
inline constexpr Xlen fs_initial = Xlen{1} << fs_shift;
inline constexpr Xlen fs_clean = Xlen{2} << fs_shift;
inline constexpr Xlen fs_dirty = Xlen{3} << fs_shift;
inline constexpr Xlen mprv = Xlen{1} << 17U;
inline constexpr Xlen sum = Xlen{1} << 18U;
inline constexpr Xlen mxr = Xlen{1} << 19U;
inline constexpr Xlen tvm = Xlen{1} << 20U;
inline constexpr Xlen tw = Xlen{1} << 21U;
inline constexpr Xlen tsr = Xlen{1} << 22U;
inline constexpr Xlen uxl = Xlen{2} << 32U;
inline constexpr Xlen sxl = Xlen{2} << 34U;
inline constexpr Xlen sd = Xlen{1} << 63U;
inline constexpr Xlen supervisor_view =
    sie | spie | spp | fs | sum | mxr | uxl | sd;
inline constexpr Xlen writable =
    sie | mie | spie | mpie | spp | mpp | fs | mprv | sum | mxr |
    tvm | tw | tsr;
inline constexpr Xlen fixed = uxl | sxl;

} // namespace mstatus_bits

inline constexpr Xlen supported_exception_delegation =
    0x000003FFULL |
    (Xlen{1} << 12U) |
    (Xlen{1} << 13U) |
    (Xlen{1} << 15U);
inline constexpr Xlen supported_interrupt_delegation =
    (Xlen{1} << 1U) | (Xlen{1} << 5U) | (Xlen{1} << 9U);
inline constexpr Xlen supported_counter_enable = 0x7U;
inline constexpr Xlen machine_isa_value =
    (Xlen{2} << 62U) | 0x141105U;

[[nodiscard]] Xlen sanitize_mstatus(Xlen value) noexcept;
[[nodiscard]] Xlen sanitize_tvec(Xlen value) noexcept;
[[nodiscard]] bool floating_point_enabled(
    const CpuSnapshot& state) noexcept;
void mark_floating_point_dirty(CpuSnapshot& state) noexcept;

class CsrFile final {
  public:
    CsrFile(CpuSnapshot& state, rv::CpuBus& bus) noexcept;

    [[nodiscard]] CsrReadResult read(
        CsrAddress address,
        PrivilegeMode privilege) noexcept;
    [[nodiscard]] CsrAccessStatus validate_write(
        CsrAddress address,
        PrivilegeMode privilege) noexcept;
    void write_validated(CsrAddress address, Xlen value) noexcept;
    [[nodiscard]] Xlen read_for_write(
        CsrAddress address,
        Xlen read_value) noexcept;

  private:
    CpuSnapshot* state_;
    rv::CpuBus* bus_;
};

} // namespace rv64
