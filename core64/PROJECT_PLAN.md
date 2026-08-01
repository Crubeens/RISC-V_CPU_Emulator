# RV64 CPU 模拟器实施计划

## 总目标

在不修改 RV32 CPU 语义、不让 RV32/RV64 执行代码互相依赖的前提下，
新增可运行 OpenSBI 与 RV64 Linux 的 RV64GC CPU。宿主程序通过
`--cpu rv32` 或 `--cpu rv64` 在运行时选择 CPU；未指定时保持 RV32。

## 解耦边界

- RV32 CPU 保持在 `core32/`，命名空间为 `rv32`。
- RV64 CPU 保持在 `core64/`，命名空间为 `rv64`。
- 两套 CPU 分别拥有寄存器、译码、执行、CSR、Trap、MMU、缓存和测试。
- RV64 不包含 RV32 的 `core.hpp`、`decode.hpp`、`execute.hpp`、`mmu.hpp`
  或 `types.hpp`。
- 允许共享的只有架构无关能力：物理地址、总线读写结果、访问宽度、
  RAM、UART、PLIC、CLINT、VirtIO、Framebuffer 和 Syscon。
- 平台通过适配层选择 CPU，不在任一 CPU 内加入 `xlen` 运行时分支。
- 每个阶段 Debug、Release 和 RV32 全量回归通过后，直接进入下一阶段。

## RV64-M1：独立 RV64I 裸机核心与运行时选择

状态：已完成并通过 Debug、Release、RV32 全量回归及 RV64I/LP64 裸机验收。

内容：

- 独立 64 位寄存器、PC、Commit 与异常结果。
- 完整 RV64I 基础整数指令，包括 `LD/SD/LWU` 和全部 W 类指令。
- 确定 x0、符号扩展、32 位结果再符号扩展、64 位移位和比较语义。
- 增加独立 RV64 Machine，复用现有外设和物理总线。
- 命令行增加 `--cpu rv32|rv64`，默认 RV32。
- 增加 RV64 裸机镜像加载和执行入口。

验收：

- 每条 RV64I 指令有正常与有意义边界测试。
- 未对齐、总线失败、非法指令、x0 和 PC 提交有确定结果。
- RV64 核心目录不包含 RV32 CPU 头文件。
- RV32 Debug/Release 全量测试不退化。
- 自编译 `rv64i/lp64` 裸机程序运行通过。

## RV64-M2：RV64M

状态：已完成并通过专项边界、RV64IM/LP64 裸机、Spike 提交轨迹及
Debug/Release 全量回归验收。

内容：

- 实现 64 位 `MUL/MULH/MULHSU/MULHU/DIV/DIVU/REM/REMU`。
- 实现 `MULW/DIVW/DIVUW/REMW/REMUW`。

验收：

- 覆盖零除、最小负数除以 -1、高半乘法和 W 类符号扩展。
- 与 Spike RV64M Commit Trace 一致。

## RV64-M3：RV64A

状态：已完成并通过 Word/DoubleWord 专项边界、RV64IMA/LP64 裸机、
Spike 提交轨迹及 Debug/Release 全量回归验收。

内容：

- 实现 `LR/SC.W`、全部 AMO.W，并按 RV64 规则符号扩展返回值。
- 实现 `LR/SC.D` 和全部 AMO.D。
- 独立维护 64 位 reservation 和 aq/rl 字段。

验收：

- 覆盖成功、失败、地址变化、普通写失效、未对齐及越界。
- 与 Spike RV64A Commit Trace 一致。

## RV64-M4：RV64C

状态：已完成并通过 RV64C 专项边界、RV64IMAC/LP64 裸机、Spike 提交轨迹及
Debug/Release 全量回归验收。

内容：

- 实现 RV64C 与 RV32C 不同的编码，包括 `C.LD/C.SD`、
  `C.ADDIW`、`C.ADDW/C.SUBW`、`C.LDSP/C.SDSP`。
- 前端改为 IALIGN=16，并正确处理跨页 32 位取指。

验收：

- RV64C 专项与边界测试通过。
- 与 Spike RV64C Commit Trace 一致。

## RV64-M5：M/S 特权、CSR、Trap 与中断

状态：已完成并通过 64 位 CSR/Trap/中断专项、RV64IMAC/Zicsr 特权裸机、
Spike 提交轨迹及 Debug/Release 全量回归验收。

内容：

- 实现 RV64 M/S CSR 宽度、WARL/WPRI、别名和计数器规则。
- 实现 ECALL、EBREAK、MRET、SRET、WFI、委托和中断优先级。
- 接入 CLINT、PLIC、UART 与 VirtIO 中断。

验收：

- OpenSBI 所需 CSR 与 Trap 链路全部有专项测试。
- M/S Trap、委托、嵌套返回、WFI 唤醒和计时器中断通过。

## RV64-M6：Sv39、TLB 与 SFENCE.VMA

状态：已完成并通过 Sv39 专项、整核取指/访存/原子链路、高半地址裸机、
Spike 提交轨迹及 Debug/Release、RV32 全量回归验收。

内容：

- 实现三级 Sv39、64 位 PTE、4 KiB/2 MiB/1 GiB 页。
- 实现 ASID、全局页、SUM、MXR、A/D 位和精确 `SFENCE.VMA`。
- 增加独立 RV64 TLB，不复用 Sv32。

验收：

- 覆盖页大小、权限、非规范地址、错误 PTE、A/D 更新和总线失败。
- TLB 命中与失效专项测试通过。
- 高半规范地址裸机程序进入 S-mode，完成翻译访存、精确失效和页故障检查。
- 与 Spike RV64 Sv39 Commit Trace 一致。

## RV64-M7：OpenSBI、Linux 与差分验收

状态：已完成并通过 RV64 OpenSBI、Linux 6.12.96、LP64 Buildroot ext4、
UART、Framebuffer、虚拟磁盘回写及 Debug/Release 全量回归验收。

内容：

- 生成 RV64 DTB 与独立启动布局。
- 编译并运行 RV64 OpenSBI、RV64 Linux 和 LP64 用户态。
- 复用架构无关的 RAM、系统总线及外设实现，不依赖 RV32 平台。
- 增加 RV64 `--boot`、`--boot-disk`、SDL、UART 输入和磁盘回写。
- 将 RV32、RV64 目录和宿主程序命名整理为明确的双架构结构。
- 保留 Spike RV64 I/M/A/C 与特权提交轨迹验收入口。

验收：

- RV32 与 RV64 均可由同一宿主程序通过 `--cpu` 选择。
- RV64 OpenSBI 输出平台信息并进入 S-mode。
- RV64 Linux 挂载 VirtIO ext4、进入 Shell、UART/Framebuffer/关机正常。
- RV32 与 RV64 Debug/Release、架构、差分和 Linux 启动测试全部通过。

已验证软件组合：

- OpenSBI v1.8.1 RV64 `fw_jump.bin`。
- Linux v6.12.96 RV64IMAC，单 Hart、Sv39、无 F/D/V。
- Buildroot 2025.02.16，RV64IMAC、LP64、musl、BusyBox、128 MiB ext4。
- 虚拟 RAM 256 MiB，VirtIO 根磁盘可读写并在退出时回写。

## RV64-M8：性能与收尾

状态：已完成并通过参考/快速逐步差分、性能基准、真实 OpenSBI S-mode
载荷、Debug/Release 102 项全量回归及 RV32 性能回归验收。

内容：

- 为 RV64 增加 TLB、译码、取指、总线和 Trap 性能统计。
- 增加参考模式与快速模式差分。
- 整理构建、镜像生成、启动和验收文档。

实现：

- 快速模式使用既有 64 项四路 Sv39 TLB 和 1024 项原始指令译码缓存。
- 参考模式直接译码，并在 Sv39 下每次执行完整页表遍历。
- 两种模式共享同一执行、CSR、Trap、权限和总线语义。
- 增加整数、访存、循环和分支的逐步 `StepResult/CpuSnapshot` 差分。
- `rv64_architecture_runner` 支持 `--reference` 轨迹。
- 整机退出时打印吞吐、Trap、取指、TLB、页表、译码、总线和
  Framebuffer 统计。
- 归档 RV32/RV64 Linux 6.12.96 与 Buildroot 2025.02.16 完整配置。

验收：

- 优化不跳过异常、权限或 CSR 检查。
- RV32 性能不因 RV64 加入而下降。
- 两种 CPU 的构建、测试和运行命令可独立复现。

验收结果：

- Release 百万步基准：参考模式约 17.59 Msteps/s，快速模式约
  21.86 Msteps/s，约 1.24 倍；数值取决于宿主机，仅作为当前基线。
- 真实 OpenSBI v1.8.1 进入 S-mode、载荷打印成功并正常 SBI 关机。
- RV32 Release 快速基准约 23.10 Msteps/s，未因 RV64 M8 发生代码路径退化。
- Debug 与 Release 均为 102/102 通过。

## RV64-M9：网络与实时时钟

状态：已完成。

- 增加架构无关 VirtIO Net 设备、RV64 平台装配、PLIC IRQ 与 DTS 节点；
- 宿主侧使用 libslirp 用户态 NAT，CPU 和设备层不依赖宿主网络 API；
- 增加 Goldfish RTC；RV32 不装配网卡，两个 CPU 保持解耦；
- Buildroot 已验证 DHCP、DNS、网关、TCP/UDP 和网络中断。

## RV64-M10：RV64GC 与 Debian 软件基线

状态：已完成。

- 完成 RV64F/RV64D 浮点状态、算术、转换、访存、CSR 与异常语义；
- 通过官方 RV64UF/RV64UD 架构测试、快速/参考差分和 Spike 差分；
- 运行 Debian 13 riscv64、LP64D、systemd、动态加载器、shell 和 APT；
- 保留 RV64IMAC Buildroot 作为无 F/D 的快速系统回归基线。

## RV64-M11：资源配置、APT 与持久化

状态：已完成（M11.1–M11.4）。

- VirtIO Block 使用文件后端，不再将大磁盘整盘复制进宿主内存；
- `--ram-mib` 支持 64–4096 MiB，并在启动前同步修改 RV64 DTB；
- 修复 Windows libslirp 空轮询回调崩溃，并以分批 `select()` 完成 HTTPS；
- Debian 官方源完成 APT 更新、安装、运行、升级、重启持久化与卸载；
- 断网、无效源、TLS 公钥错误、空间不足和损坏包均确定失败，dpkg 数据库
  保持完整；
- Debug/Release 133/133 通过，真实 APT 验收期间网络丢包与轮询错误均为 0。

## RV64-M12：CPU 性能优化

状态：已完成。

目标是在不改变架构语义、异常精度或 RV32 实现的前提下，提高 OpenSBI、Linux、
Debian 和 Framebuffer 工作负载的 RV64 吞吐。

实施边界：

- 每项优化必须保留 Reference/Fast 逐步差分与官方架构测试；
- 不缓存不可缓存设备区，不跳过地址翻译、权限、A/D 位、Trap 或中断检查；
- `FENCE.I`、`SFENCE.VMA`、SATP、特权级和自修改代码必须有确定失效语义；
- 先以可重复微基准确认热点，再用 Linux 启动统计验证真实收益；
- 每个优化子阶段单独提交，若无稳定收益则撤销该优化。

计划：

1. M12.1：已完成。为 RV64 增加带地址空间与特权上下文的 4096 项指令前端缓存，
   减少高命中 Linux 代码路径中的重复翻译、总线读取和译码；`FENCE.I`、
   `SFENCE.VMA`、复位和模式切换均覆盖失效测试。当前宿主千万步快速微基准由
   约 18.32 提升至 28.56 Msteps/s，真实 Linux 2 亿步达到约 11.99 Msteps/s；
2. M12.2：已完成。网络后端通过架构无关的轮询周期接口合并无变化的 libslirp
   调用；发送帧后仍在下一 machine-step 强制处理。真实 Linux 2 亿步吞吐率由约
   11.99 提升至 12.78 Msteps/s，后端调用由 2 亿次降至约 2 万次；无稳定收益的
   固定设备直调方案已撤回，未进入提交；
3. M12.3：已完成。依据 Linux 前端未命中统计将缓存容量由 4096 调至 16384 项，
   命中率由约 83.9% 提高到 92.2%，取指总线读取由约 4600 万次降到 2217 万次，
   吞吐率由 12.78 提高到 13.81 Msteps/s；32768 项收益不足 1% 且内存占用翻倍，
   因此不采用；缓存改由 Core 独占堆内存保存，消除宿主栈大小限制。M12 的三个
   子阶段至此完成。
