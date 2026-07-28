# RV64-M1 bare-metal acceptance image

Build the image in Ubuntu/WSL with the Linux RISC-V cross toolchain:

```sh
PROJECT=/mnt/c/Users/Lenovo/Desktop/files/RISC-V_CPU_Emulator
OUT="$PROJECT/build/debug/baremetal64"
mkdir -p "$OUT"
riscv64-linux-gnu-gcc \
  -march=rv64i -mabi=lp64 \
  -nostdlib -nostartfiles -static \
  -Wl,--build-id=none -Wl,--no-relax \
  -Wl,-T,"$PROJECT/tests/baremetal64/link.ld" \
  -o "$OUT/smoke.elf" \
  "$PROJECT/tests/baremetal64/start.S"
riscv64-linux-gnu-objcopy \
  -O binary "$OUT/smoke.elf" "$OUT/smoke.bin"
```

Run it from PowerShell:

```powershell
.\build\debug\rv32_emulator.exe --cpu rv64 --run-raw `
  .\build\debug\baremetal64\smoke.bin 1000
```

Success is reported only when the RV64I/LP64 program reaches its final
`EBREAK`; any failed self-check executes an illegal instruction instead.
