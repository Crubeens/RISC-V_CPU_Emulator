# Spike reference revision

- Repository: `https://github.com/riscv-software-src/riscv-isa-sim.git`
- Commit: `520a5f185083ac3c97b751501dfac02a6c1f5970`
- ISA used by the differential runner:
  `rv32imac_zicsr_zifencei`
- Privilege modes used by the differential runner: `msu`

This exact revision was built outside the project tree and used for the first
live differential acceptance run:

| Coverage | Test | Compared DUT commits |
| --- | --- | ---: |
| RV32I environment | `rv32ui-simple` | 77 |
| RV32I arithmetic | `rv32ui-add` | 501 |
| RV32M | `rv32um-mul` | 495 |
| RV32A | `rv32ua-amoadd_w` | 102 |
| RV32C | `rv32uc-rvc` | 255 |

All 1430 DUT commits matched Spike. For each test, Spike emitted another 4995
commits while polling the test's `tohost` completion loop; those records
occurred after the complete DUT trace and were not part of the comparison.
