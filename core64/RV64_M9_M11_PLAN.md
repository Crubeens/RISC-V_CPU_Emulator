# RV64 M9–M11 后续实施计划

状态：当前唯一的 RV64 后续实施计划。

本文件承接已经冻结并完成的
[`RV64-M1` 至 `RV64-M8`](PROJECT_PLAN.md)，不改写这些阶段的实现与验收
结论。后续按 M9、M10、M11 顺序推进；每个子阶段测试通过后提交，再进入下一
子阶段。

## 最终目标

在保持 RV32/RV64 CPU 解耦、CPU/外设解耦的前提下：

1. 为 RV64 Machine 提供可联网的 VirtIO 网卡和准确实时时钟。
2. 将 RV64 CPU 从 RV64IMAC 完整扩展到 RV64GC 所需的 RV64F、RV64D。
3. 运行 Debian 13 riscv64/LP64D 用户态，使用官方软件源完成 APT 验收。
4. 为实际安装应用提供足够且可配置的 RAM、文件后端磁盘和确定的持久化行为。

## 不可破坏的边界

- `core32/` 和 `core64/` 不共享译码、执行、CSR、Trap、MMU 或寄存器状态。
- `core32/` 不增加浮点寄存器，也不装配网络设备。
- CPU 只通过架构无关总线和中断线访问设备，不包含 VirtIO、libslirp、
  RTC、PLIC、SDL 或宿主网络头文件。
- VirtIO Net 位于 `devices/`；libslirp 位于宿主适配层；只有
  `platform64/` 决定是否装配网卡。
- RV32/RV64 的 DTS、镜像、启动命令和测试产物使用独立名称，不互相覆盖。
- 不通过跳过非法指令、伪造浮点结果或把 LP64D 用户态改成非官方软浮点 ABI
  来绕过 CPU 实现。

## RV64-M9：网络设备、用户态 NAT 与 RTC

状态：已完成设备和平台实现；Debian 公网 HTTPS 与 APT 留到 M11 做最终验收。

已实现：

- 架构无关的 legacy VirtIO-MMIO Net，包含独立 RX/TX virtqueue、DMA、
  used ring、中断确认、复位、MAC 和链路状态。
- 网络硬件模型与以太网帧后端分离，CPU 和设备层不依赖 libslirp。
- RV64 专用 MMIO 地址、PLIC IRQ、Machine 装配和 DTS 节点；RV32 未装配。
- Windows UCRT64 下的 libslirp 用户态 NAT，不要求 TAP、网桥或管理员权限。
- Goldfish RTC，供 Linux 使用宿主实时时间。

已有验收证据：

- Buildroot RV64IMAC 启动并识别 `virtio_net`。
- DHCP 地址为 `10.0.2.15`，默认网关为 `10.0.2.2`，DNS 为 `10.0.2.3`。
- Debug/Release 自动测试和 RV32 回归通过。

阶段关闭条件：

- VirtIO 队列、错误描述符、短缓冲、DMA 失败、复位和中断有确定测试。
- 网络关闭或宿主后端不可用时，模拟器仍可启动且不会阻塞 CPU。
- 公网 DNS、HTTPS 和 Debian 仓库访问在 M11 的真实系统环境统一验收。

## RV64-M10：完整 RV64F/RV64D

状态：未开始，是当前下一阶段。

### M10.1：状态、CSR、访存、移动和压缩访存

- 增加独立的 32×64 位浮点寄存器文件。
- 实现 `fflags`、`frm`、`fcsr`、`mstatus.FS`、`sstatus.FS` 和 `SD` 汇总位。
- 实现 `FLW/FSW/FLD/FSD`、整数/浮点位移动指令。
- 实现 `C.FLD/C.FSD/C.FLDSP/C.FSDSP`。
- 实现 NaN-boxing；FS=Off 时浮点指令产生非法指令，写浮点状态后进入 Dirty。

验收：

- 正常、未对齐、页故障、总线失败和跨页访问均有测试。
- CSR 权限、别名、FS 状态转换和 Trap 精确性均有测试。
- Debian 动态加载器当前首个失败点 `C.FSD fs0,112(a0)` 能按规范执行。

### M10.2：算术、融合运算与舍入

- 实现 F/D 的加、减、乘、除、平方根和 fused multiply-add 指令族。
- 支持 RNE、RTZ、RDN、RUP、RMM 和动态舍入模式。
- 正确累计 NV、DZ、OF、UF、NX。
- 明确定义 NaN、无穷、正负零和 subnormal 的位级语义。
- 使用固定版本、确定性的软浮点实现，不依赖宿主浮点环境。

验收：

- 每种舍入模式、异常标志和特殊值均有边界测试。
- 结果位模式和 `fflags` 与 Spike/官方浮点测试一致。

### M10.3：比较、分类、符号与转换

- 实现 `FSGNJ*`、`FMIN/FMAX`、`FEQ/FLT/FLE`、`FCLASS`。
- 实现 S/D 互转。
- 实现 W/WU/L/LU 与 S/D 的全部整数浮点转换。

验收：

- 覆盖精确边界、溢出、NaN、无穷、符号扩展和饱和值。
- RV64F 与 RV64D 指令集合不存在仅为启动样例补齐的缺口。

### M10.4：整核与软件栈验收

- 完成 F/D 裸机、架构测试和 Spike Commit Trace 差分。
- Debug、Release、RV32 全量和 RV64IMAC 原有测试全部通过。
- OpenSBI、DTS 和 Linux ISA 宣告只有在实现完成后才更新为实际支持的
  `rv64imafdc`。
- Debian 13 PID 1、动态加载器、systemd、Shell 和 DHCP 正常运行。

## RV64-M11：Debian、APT 与可长期使用的资源配置

状态：前置镜像、VirtIO 网络和 RTC 已准备；等待 M10 完成。

软件基线：

- Debian 13 `riscv64`、ELF64、LP64D、RV64GC。
- 根文件系统由 `scripts/build_debian_rv64_rootfs.sh` 生成，默认输出
  `boot-images/debian-13-riscv64-apt.ext4`。
- 使用 Debian 官方 riscv64 软件源，不维护私有 RV32 包仓库。

实现内容：

- 完成公网 DNS、TCP/UDP、HTTPS 和 CA 证书链验收。
- 提供可配置的 RV64 RAM；默认值和上限必须与实际 DTB 一致。
- VirtIO Block 增加文件后端，避免把大磁盘镜像完整复制到宿主内存。
- 支持可扩展的稀疏 ext4 磁盘，并保证正常关机后的数据持久化。
- 保留 RV64IMAC Buildroot 作为无 F/D、无 APT 的快速回归基线。

最终验收：

- `date -u`、`rtc0`、IP 地址、默认路由和 DNS 正常。
- 通过 HTTPS 访问 Debian 官方仓库，`apt update` 成功。
- 安装、运行、升级并卸载至少一个小型软件包。
- 关机后磁盘正确回写，再次启动后包数据库和已安装文件保持一致。
- 断网、错误源、证书错误、磁盘空间不足和损坏包均给出确定错误，不破坏
  已安装系统。
- 大磁盘不按镜像容量等量占用宿主 RAM；RAM 和磁盘配置与 Linux 报告一致。
- Debug/Release、RV32 全量、RV64 Buildroot、OpenSBI/Linux 和网络设备测试
  全部通过。

## 提交规则

- M10.1、M10.2、M10.3、M10.4 和 M11 各形成至少一个可构建、可测试的提交。
- 每次提交记录新增指令/设备行为、测试范围、镜像要求和已知限制。
- OpenSBI、Linux、根文件系统和其他大型构建产物保留在 `boot-images/`，
  不提交 Git；可复现配置、脚本和哈希说明提交 Git。
