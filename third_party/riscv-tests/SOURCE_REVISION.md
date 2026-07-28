# Vendored riscv-tests subset

Source: https://github.com/riscv-software-src/riscv-tests

- `riscv-tests` revision: `ec8e5a29845b97b515299b89c523831b41367cda`
- `riscv-test-env` revision: `6de71edb142be36319e380ce782c3d1830c65d68`

The vendored files are limited to the RV32 I/M/A/C physical-environment
instruction tests, their shared RV64 source bodies, scalar macros, encoding
definitions, linker environment, and BSD license. The project supplies its
own linker layout and test runner under `tests/architecture`.

`rv32ui-ma_data` is intentionally not selected: that optional case assumes
hardware-completed misaligned loads and stores, while this DUT specifies and
tests deterministic address-misaligned exceptions. All selected aligned
RV32 I/M/A tests and the upstream RV32C `rvc` test are executed.
