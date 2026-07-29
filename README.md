# RISC-V CPU Emulator

这是一个以自研 RV32 CPU 核心为中心、外设可替换的整机模拟器项目。

当前 RV32 版本为 **M9 基线版**：能够运行 RV32IMAC 裸机程序，通过适用的
官方 `riscv-tests` I/M/A/C 用例，并通过 OpenSBI 启动 Linux、挂载
VirtIO ext4 根文件系统。SDL 窗口可以显示 UART 终端或线性
Framebuffer，键盘输入会送入虚拟 UART。

RV64 开发已完成 **RV64-M6**：新增独立 RV64IMAC 核心、M/S/U
特权级、64 位 CSR、Trap/中断链路、RV64 Machine、运行时 CPU 选择和
LP64 裸机入口，并已实现三级 Sv39、64 位 PTE、独立 TLB 和
`SFENCE.VMA`。RV64 的 OpenSBI/Linux 启动属于后续阶段，当前仍不能用
RV64 启动 Linux。

## 当前包含

- 完整 RV32I 取指、译码、执行、访存和精确提交。
- RV32M 乘除法、RV32A Word 原子指令和完整整数 RV32C 压缩指令。
- 16 位对齐取指、跨 4 字节边界取指以及跨页第二半字异常处理。
- Zicsr、Zifencei 和 64 位 Zicntr 计数器。
- M/S/U 特权级、Trap 委托、`MRET`、`SRET` 和 `WFI`。
- 无 TLB 的 Sv32 两级页表、4 KiB 页和 4 MiB 超级页。
- `SUM`、`MXR`、`MPRV`、A/D 位更新和 `SFENCE.VMA`。
- RAM、CLINT、PLIC、NS16550A UART、Legacy VirtIO MMIO Block、
  SYSCON 和 640×480 XRGB8888 Framebuffer。
- OpenSBI/Linux 固定启动布局、DTB、VirtIO ext4 根文件系统和磁盘回写。
- SDL2 图形窗口、80×30 UART 终端、Framebuffer 视图和键盘输入。
- 60 个适用于本机配置的官方 `riscv-tests` RV32 I/M/A/C 用例。
- Debug/Release 自动测试。
- RV64I：64 位寄存器、完整 RV64I 基础整数指令、LD/SD/LWU、W 类指令、
  独立平台适配层和裸机测试。
- RV64M：64 位及 W 类乘法、三种高半乘法、除法与余数，并覆盖除零和
  最小负数除以 `-1` 的边界语义。
- RV64A：LR/SC.W、LR/SC.D、全部 AMO.W/AMO.D、aq/rl 译码，以及
  reservation 成功、失败和失效语义。
- RV64C：全部整数压缩指令、IALIGN=16、半字取指和跨页 32 位取指。
- RV64 特权态：M/S/U 模式、Zicsr、异常与中断委托、`MRET/SRET/WFI`、
  CLINT/PLIC/UART/VirtIO 中断采样和 64 位计数器。
- RV64 Sv39：三级页表、4 KiB/2 MiB/1 GiB 页、非规范地址检查、
  `SUM/MXR/MPRV`、A/D 自动更新、16 位 ASID、全局页、独立 64 项 TLB
  和四种 `SFENCE.VMA` 失效范围。

RV32 CPU 核心位于 `core/`，RV64 CPU 核心位于 `core64/`；两者都只依赖
抽象总线，不依赖 SDL 或任何具体外设实现。

## 构建

需要 CMake、Ninja、Clang、DTC、SDL2 开发库以及
`riscv32-unknown-elf-gcc/objcopy`。Windows 默认会从
`C:\msys64\ucrt64` 查找 MSYS2 UCRT64 依赖。

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release
```

只运行官方架构测试：

```text
ctest --preset debug -L architecture
```

RV64 裸机测试需要在 Ubuntu/WSL 中使用 `riscv64-linux-gnu-gcc` 和
`riscv64-linux-gnu-objcopy` 生成镜像，完整命令见
[`tests/baremetal64/README.md`](tests/baremetal64/README.md)。生成后可运行：

```powershell
build\debug\rv32_emulator.exe --cpu rv64 --run-raw `
  build\debug\baremetal64\smoke.bin 1000
```

运行时 CPU 选择：

```powershell
build\debug\rv32_emulator.exe --cpu rv32
build\debug\rv32_emulator.exe --cpu rv64
```

省略 `--cpu` 时默认使用 RV32。RV64 还支持 `--load-images` 验证三段
镜像的加载布局；`--boot` 和 `--boot-disk` 要等 RV64 特权态阶段完成后启用。

## 提交轨迹与 Spike 差分

`Core::step()` 会为每条真正退休的指令返回一条提交轨迹。架构测试
runner 可以直接输出轨迹：

```powershell
build/debug/rv32_architecture_runner.exe --trace `
  build/debug/architecture/rv32ui-simple.bin
```

配置阶段如果同时找到 Python 3 和 `spike`，CMake 会增加真实的 Spike
逐指令差分测试；缺少 Spike 不会影响普通构建和测试：

```text
ctest --preset debug -L differential --output-on-failure
```

也可以通过 `-DRV32_SPIKE_EXECUTABLE=<spike路径>` 显式指定 Spike。
官方架构测试写入 `tohost` 后，DUT 会立即结束，而 Spike 可能在下一次
宿主接口轮询前继续执行结束循环。差分工具使用 `--allow-spike-tail`
只忽略完整 DUT 提交前缀之后的 Spike 记录；前缀中的权限级、PC、指令
和通用寄存器写回仍会逐条严格比较。
Spike 以 `--priv=msu` 运行，因为 riscv-tests 物理环境会访问 `satp`
并通过 `mret` 进入低特权级；限制为 M 模式会造成伪非法指令差异。
首次真实验证所用的固定 Spike 提交记录在
`tests/differential/SPIKE_REVISION.md`。找到本机 Spike 时，差分标签会
覆盖 RV32I、RV32M、RV32A 和 RV32C 的代表用例。

## 启动 Linux

串口加持久化虚拟磁盘：

```powershell
build/release/rv32_emulator.exe --boot-disk `
  boot-images/opensbi-v1.8.1-rv32-fw_jump.bin `
  boot-images/linux-v6.12.96-rv32ima-Image `
  build/release/images/rv32-virt.dtb `
  boot-images/buildroot-2025.02.16-rv32ima-rootfs.ext4
```

启用图形窗口只需在启动模式前增加 `--gui`：

```powershell
build/release/rv32_emulator.exe --gui --boot-disk `
  boot-images/opensbi-v1.8.1-rv32-fw_jump.bin `
  boot-images/linux-v6.12.96-rv32ima-Image `
  build/release/images/rv32-virt.dtb `
  boot-images/buildroot-2025.02.16-rv32ima-rootfs.ext4
```

- `F1`：UART 终端。
- `F2`：Linux/裸机 Framebuffer。
- `F2` 显示的是客户机实际写入的显存，不是 `F1` 的复制画面。Linux
  内核必须内建 `CONFIG_FB=y`、`CONFIG_FB_SIMPLE=y`、
  `CONFIG_FRAMEBUFFER_CONSOLE=y`、`CONFIG_VT=y` 和
  `CONFIG_VT_CONSOLE=y`；否则 Framebuffer 保持全黑。
- 关闭 SDL 窗口不会强制中断客户机，模拟器会继续使用宿主终端。
- 最后可追加正整数作为最大 machine-step 数；省略时
  `--boot-disk` 持续运行到客户机请求关机。

## 目录

```text
core/          自研 CPU 核心
core64/        独立 RV64IMAC 特权 CPU 核心及 RV64 实施计划
common/        RV32/RV64 共用的架构无关总线类型
platform/      总线、启动布局和整机装配
platform64/    RV64 Machine 适配层
devices/       可替换外设模型
app/           命令行与 SDL 前端
tests/         单元、裸机、架构和整机测试
third_party/   固定版本的测试依赖
docs/          原有学习资料
prestudy/      原有前置学习代码
```

RV32 后续内容按照 [PROJECT_PLAN.md](PROJECT_PLAN.md) 继续；RV64 按照
[`core64/PROJECT_PLAN.md`](core64/PROJECT_PLAN.md) 的固定阶段继续实现。
