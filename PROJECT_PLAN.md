# RISC-V32 CPU 模拟器项目最终计划书

版本：1.0

冻结日期：2026-07-24

状态：项目最终实施基线

## 1. 项目目标

开发一个能够运行 RV32 Linux 的完整系统模拟器，最终具备：

1. 自研的 RV32 CPU 核心。
2. 可加载 OpenSBI、设备树和自行交叉编译的 Linux 内核。
3. 可初始化并挂载 VirtIO 虚拟磁盘。
4. UART 终端和 Framebuffer 图形界面。
5. 可测试、可调试、可逐步扩展的模块化结构。

项目的核心工作限定为 CPU 及 CPU 面向虚拟硬件的稳定接口。RAM、UART、CLINT、PLIC、VirtIO、SYSCON、Framebuffer 和图形窗口采用经过验证的既有设计，经过适配后部署在平台层。

## 2. 固定技术范围

### 2.1 CPU 最小 Linux 目标

- 单 Hart。
- Little Endian。
- 顺序解释执行。
- RV32IMA。
- Zicsr。
- Zifencei。
- Zicntr。
- M/S/U 三种特权级。
- Sv32 虚拟内存。
- 精确异常和中断。
- 64 位 `cycle`、`time`、`instret` 计数器及 RV32 高低位 CSR。

### 2.2 首次 Linux 启动后增加

- C 压缩指令扩展。
- 小型 TLB。
- SDL 图形窗口和键盘输入。
- GDB Remote Stub。
- 性能分析和指令执行优化。

### 2.3 第一阶段明确不实现

- F、D、V、H 扩展。
- 多 Hart/SMP。
- 流水线和周期精确微架构。
- Cache 一致性。
- JIT。

## 3. 不可破坏的架构边界

CPU 核心只能依赖以下接口：

1. 物理总线读写。
2. LR/SC 和 AMO 原子操作。
3. 六条 M/S 软件、定时器、外部中断输入线。
4. `time` 时钟源。
5. Reset 配置。
6. Snapshot 和 Commit Trace 调试接口。

CPU 不允许：

- 持有任何具体外设对象。
- 包含 UART、PLIC、CLINT、VirtIO 或 GUI 头文件。
- 调用设备 `tick()`。
- 读取磁盘文件。
- 直接操作 SDL。
- 允许总线反向修改 CPU 的异常或 CSR。

总线只返回结构化错误，CPU 负责将其转换为架构异常。

## 4. 模块划分

```text
core/
  自研 CPU 状态、译码、执行、CSR、Trap、MMU

platform/
  统一总线、DMA、整机装配、设备调度、镜像加载

devices/
  RAM、CLINT、PLIC、UART、VirtIO Block、SYSCON、Framebuffer

app/
  命令行入口和未来的 GUI 入口

tests/
  单元测试、架构测试、差分测试、Linux 集成测试
```

原有 `docs/` 和 `prestudy/` 仅作为学习资料保留，不参与正式模块，也不修改其中已有文件。

## 5. 固定物理地址布局

| 设备 | 基地址 | 范围/说明 |
|---|---:|---|
| CLINT | `0x02000000` | 64 KiB |
| PLIC | `0x0C000000` | 64 MiB 窗口 |
| NS16550A UART | `0x10000000` | 256 B，IRQ 10 |
| VirtIO Block | `0x10001000` | 4 KiB，IRQ 1 |
| SYSCON | `0x11100000` | 4 KiB |
| Framebuffer | `0x40000000` | 大小由分辨率决定 |
| DRAM | `0x80000000` | 默认 64 MiB |

Linux Image 默认放置在 `0x80400000`，满足 RV32 内核 4 MiB 对齐要求。OpenSBI/BIOS 从 `0x80000000` 启动，DTB 放置在 RAM 顶部的安全区域。

## 6. 实施阶段与验收条件

### M0：整机框架和外设部署

内容：

- CMake 工程。
- `rv32_core` 稳定接口和 Reset/Snapshot 状态。
- 统一物理总线。
- RAM、CLINT、PLIC、UART、VirtIO Block、SYSCON、Framebuffer。
- Machine 装配层。
- 第三方来源和许可证记录。

验收：

- Debug 和 Release 均可构建。
- 所有框架测试通过。
- 未修改任何原有文件。
- CPU 尚不执行正式指令。

### M1：完整 RV32I

内容：

- Fetch/Decode/Execute/Commit。
- 全部 RV32I 指令。
- 对齐和访问错误。
- 非法指令。
- 精确提交。

验收：

- RV32I 单元测试和架构测试通过。
- 任何失败访存均不会产生部分提交。

### M2：M、A、Zicsr、Zifencei、Zicntr

验收：

- 乘除法全部边界条件通过。
- LR/SC、全部 RV32 Word AMO 通过。
- CSR 权限和读改写语义通过。

### M3：特权级、异常和中断

内容：

- M/S/U。
- Trap 委托。
- `MRET`、`SRET`、`WFI`。
- 中断优先级和向量入口。

验收：

- Machine/Supervisor 特权测试通过。
- CLINT 和 PLIC 能通过接口驱动 CPU Trap。

### M4：Sv32

内容：

- 两级页表。
- 4 KiB 页和 4 MiB 超级页。
- `SUM`、`MXR`、`MPRV`。
- A/D 位。
- `SFENCE.VMA`。

验收：

- 地址翻译、权限、Page Fault 和 Access Fault 测试通过。

第一版 MMU 不使用 TLB，以正确性优先。

### M5：OpenSBI 和 Linux 串口启动

验收：

- 显示 OpenSBI Banner。
- Linux 输出 `Linux version`。
- UART 控制台可交互。

### M6：VirtIO 根文件系统

验收：

- 初始化虚拟磁盘。
- Linux 识别 VirtIO Block。
- 挂载根文件系统并进入 Shell。
- 磁盘写入可以持久化。

### M7：C 扩展和图形界面

验收：

- RV32IMAC 软件通过架构测试。
- SDL 窗口显示 UART 终端或 Linux Framebuffer。
- 键盘输入能够送入 UART。

### M8：调试和优化

内容：

- Spike/QEMU 差分测试。
- GDB Remote Stub。
- TLB。
- 性能统计。
- 可选解释器优化。

## 7. 测试策略

测试分为四层：

1. 单元测试：位操作、译码、CSR、MMU、各外设寄存器。
2. 架构测试：RISC-V Architecture Test 和兼容的 `riscv-tests`。
3. 差分测试：逐指令比较 PC、寄存器、内存写入和 Trap。
4. 集成测试：OpenSBI、Linux、VirtIO 根文件系统和 GUI。

所有阶段必须先通过自动测试，再进入下一阶段。

## 8. 第三方代码策略

外设行为参考 `bane9/rv64gc-emu`，其 LICENSE 为 MIT。项目只吸收设备模型思想和必要实现，不引入其 CPU、CSR、MMU 或指令执行代码。

所有吸收或改写的第三方实现必须：

- 在 `THIRD_PARTY_NOTICES.md` 中记录来源。
- 保留许可证文本。
- 修复越界、未对齐访问、CPU 反向耦合和宿主退出等不适合本项目的问题。

## 9. GitHub 交付策略

每个里程碑形成独立、可构建、可测试的提交。首次 GitHub 提交对应 M0 框架版；在用户提供目标账号和仓库信息后推送，不在提交中包含构建产物、磁盘镜像、账号或密钥。
