# RISC-V32 CPU Emulator

这是一个以自研 RV32 CPU 核心为中心、外设可替换的整机模拟器项目。

当前版本为 **M7 基线版**：能够运行 RV32IMAC 裸机程序，通过适用的
官方 `riscv-tests` I/M/A/C 用例，并通过 OpenSBI 启动 Linux、挂载
VirtIO ext4 根文件系统。SDL 窗口可以显示 UART 终端或线性
Framebuffer，键盘输入会送入虚拟 UART。

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

CPU 核心位于 `core/`，只依赖抽象总线和中断线，不依赖 SDL 或任何
具体外设实现。

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

## M8 提交轨迹与 Spike 差分

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
platform/      总线、启动布局和整机装配
devices/       可替换外设模型
app/           命令行与 SDL 前端
tests/         单元、裸机、架构和整机测试
third_party/   固定版本的测试依赖
docs/          原有学习资料
prestudy/      原有前置学习代码
```

后续内容按照 [PROJECT_PLAN.md](PROJECT_PLAN.md) 的固定阶段继续实现。
