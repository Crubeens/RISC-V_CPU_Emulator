# RISC-V32 CPU Emulator

这是一个以自研 RV32 CPU 核心为中心、外设可替换的整机模拟器项目。

当前版本为 **M4 基线版**：已经完成 RV32IMA、Zicsr、Zifencei、
Zicntr、M/S/U 特权级、异常与中断以及 Sv32；下一阶段为
OpenSBI 和 Linux 串口启动。

## 当前包含

- 完整 RV32I 取指、译码、执行、访存和精确提交。
- RV32M 乘除法与 RV32A Word 原子指令。
- Zicsr、Zifencei 和 64 位 Zicntr 计数器。
- M/S/U 特权级、Trap 委托、`MRET`、`SRET` 和 `WFI`。
- 无 TLB 的 Sv32 两级页表、4 KiB 页和 4 MiB 超级页。
- `SUM`、`MXR`、`MPRV`、A/D 位更新和 `SFENCE.VMA`。
- Instruction/Load/Store Page Fault 与页表 Access Fault。
- 六条 M/S 软件、定时器、外部中断输入线及向量入口。
- 独立的 `rv32_core`，只通过抽象总线和中断线连接平台。
- 统一物理地址总线、DMA、LR/SC 和 AMO 接口。
- RAM、CLINT、PLIC、NS16550A UART、Legacy VirtIO MMIO Block、
  SYSCON 和线性 Framebuffer。
- 整机 `Machine` 装配层和 RV32I 交叉编译裸机测试。
- Debug/Release 自动测试。

## 当前不包含

- OpenSBI/Linux 启动。
- VirtIO 根文件系统启动与磁盘持久化流程。
- C 压缩指令扩展。
- SDL 图形窗口。

后续内容按照 [PROJECT_PLAN.md](PROJECT_PLAN.md) 的固定阶段继续实现。

## 构建

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release
```

命令行演示程序：

```text
build/debug/rv32_emulator
```

## 目录

```text
core/       自研 CPU 核心
platform/   总线和整机装配
devices/    可替换外设模型
app/        命令行入口
tests/      自动测试
docs/       原有学习资料（保持不动）
prestudy/   原有前置学习代码（保持不动）
```
