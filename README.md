# RISC-V CPU Emulator

这是一个同时支持 RV32 与 RV64、以自研 CPU 核心为中心的整机模拟器。
两套 CPU 完全独立，RAM、系统总线和虚拟外设由平台层共享；运行时通过
`--cpu rv32` 或 `--cpu rv64` 选择 CPU，省略时默认使用 RV32。

当前状态：

- RV32：M9 基线，支持 RV32IMAC、M/S/U、Sv32、OpenSBI、Linux、
  Buildroot ext4 根文件系统和 SDL 图形界面。
- RV64：M7 完成，支持 RV64IMAC、M/S/U、Sv39、OpenSBI、Linux、
  LP64 Buildroot ext4 根文件系统和 SDL 图形界面。
- Debug 与 Release 自动测试均覆盖两种架构；当前测试总数为 100。

## 架构边界

| 模块 | 用途 |
| --- | --- |
| `core32/` | 独立 RV32IMAC CPU、CSR、Trap、中断、Sv32 和 TLB |
| `core64/` | 独立 RV64IMAC CPU、CSR、Trap、中断、Sv39 和 TLB |
| `common/` | 与 XLEN 无关的总线接口、物理地址和系统总线 |
| `devices/` | RAM、CLINT、PLIC、UART、VirtIO Block、Framebuffer、Syscon |
| `platform32/` | RV32 地址布局、设备树、启动装载和 Machine |
| `platform64/` | RV64 地址布局、设备树、启动装载和 Machine |
| `app/` | 统一命令行入口、终端和 SDL 前端 |
| `tests/` | 两种架构的单元、裸机、差分、设备树和整机测试 |

CPU 核心只依赖抽象总线，不包含 SDL，也不依赖任何具体外设实现。
RV32 与 RV64 不共享译码、执行、CSR、Trap 或 MMU 代码，避免通过运行时
`xlen` 分支把两套 CPU 重新耦合。

## 已实现能力

- RV32I/RV64I 基础整数指令和 RV64 W 类指令。
- RV32M/RV64M、RV32A/RV64A、RV32C/RV64C。
- Zicsr、Zifencei、Zicntr。
- M/S/U 特权级、精确异常、中断委托、`MRET`、`SRET` 和 `WFI`。
- Sv32 与 Sv39、页权限、A/D 位、ASID、TLB 和 `SFENCE.VMA`。
- OpenSBI 固件、Linux 内核和 DTB 的固定启动布局。
- Legacy VirtIO MMIO Block 和可回写的 ext4 虚拟磁盘。
- NS16550A UART、CLINT、PLIC、Syscon 和 640×480 XRGB8888 Framebuffer。
- SDL2 窗口、UART 终端、Framebuffer 显示和键盘输入。
- RV32 官方 `riscv-tests` I/M/A/C 用例及 Spike Commit Trace 差分。

## 构建和测试

Windows 构建需要 CMake、Ninja、Clang、DTC、SDL2，以及用于 RV32
自动裸机/架构测试的 `riscv32-unknown-elf-gcc` 和 `objcopy`。
默认从 MSYS2 UCRT64 的 `C:\msys64\ucrt64` 查找宿主依赖。

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release
```

常用测试：

```powershell
ctest --preset debug -L architecture --output-on-failure
ctest --preset debug -L differential --output-on-failure
```

没有找到 Spike 时只跳过实时差分测试，不影响其他构建和测试。
可使用 `-DRV32_SPIKE_EXECUTABLE=<spike路径>` 显式指定 Spike。
首次验证使用的 Spike 修订见
[`tests/differential/SPIKE_REVISION.md`](tests/differential/SPIKE_REVISION.md)。

RV64 裸机镜像由 WSL 中的 RISC-V Linux 工具链生成，步骤见
[`tests/baremetal64/README.md`](tests/baremetal64/README.md)。

## 运行

查看命令：

```powershell
.\build\release\riscv_emulator.exe --help
```

RV32 Linux：

```powershell
.\build\release\riscv_emulator.exe --cpu rv32 --boot-disk `
  .\boot-images\opensbi-v1.8.1-rv32-fw_jump.bin `
  .\boot-images\linux-v6.12.96-rv32ima-Image `
  .\build\release\images\rv32-virt.dtb `
  .\boot-images\buildroot-2025.02.16-rv32ima-rootfs.ext4
```

RV64 Linux：

```powershell
.\build\release\riscv_emulator.exe --cpu rv64 --boot-disk `
  .\boot-images\opensbi-v1.8.1-rv64-fw_jump.bin `
  .\boot-images\linux-v6.12.96-rv64imac-Image `
  .\build\release\images\rv64-virt.dtb `
  .\boot-images\rootfs-buildroot-v2025.02.16-rv64imac.ext4
```

在 `--boot-disk` 前增加 `--gui` 可启用 SDL 窗口，例如：

```powershell
.\build\release\riscv_emulator.exe --cpu rv64 --gui --boot-disk `
  .\boot-images\opensbi-v1.8.1-rv64-fw_jump.bin `
  .\boot-images\linux-v6.12.96-rv64imac-Image `
  .\build\release\images\rv64-virt.dtb `
  .\boot-images\rootfs-buildroot-v2025.02.16-rv64imac.ext4
```

- `F1`：UART 终端视图。
- `F2`：客户机 Framebuffer 视图。
- 键盘输入发送到虚拟 UART。
- 关闭 SDL 窗口后客户机仍可在宿主终端中继续运行。
- 可在命令末尾追加正整数作为 machine-step 上限；省略时运行到客户机
  请求关机。
- `boot-images/` 保存外部编译得到的持久镜像，不属于 `build/`，
  重新配置或清理 CMake 构建目录不会覆盖它。

完整启动命令也记录在 [`docs/启动命令.txt`](docs/启动命令.txt)。

## 阶段计划

- RV32 固定计划：[PROJECT_PLAN.md](PROJECT_PLAN.md)
- RV64 固定计划：[core64/PROJECT_PLAN.md](core64/PROJECT_PLAN.md)

下一阶段是 RV64-M8：性能统计、参考/快速路径差分以及最终可复现性收尾。
